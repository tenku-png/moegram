#include "moegram/bridge/media.h"

#include "api/api_common.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "core/application.h"
#include "data/data_chat_participant_status.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_media_types.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "plugins/telegram_event_adapter.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"
#include "ui/chat/attach/attach_prepare.h"
#include "styles/style_boxes.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMimeDatabase>
#include <QtCore/QStandardPaths>

namespace MoeGram::Bridge {
namespace {

constexpr auto kDownloadTimeout = 10 * 60 * crl::time(1000);
constexpr auto kMaxDocumentSize = int64(512 * 1024 * 1024);
constexpr auto kMaxVideoNoteSize = int64(64 * 1024 * 1024);

using Request = Plugins::MediaRequest;

Main::Session *ActiveSession() {
	auto &account = Core::App().activeAccount();
	return account.sessionExists() ? &account.session() : nullptr;
}

QString MediaFolder() {
	const auto result = QDir::tempPath() + u"/moegram-plugin-media"_q;
	QDir().mkpath(result);
	return result;
}

void Resolve(const Request &request, QString path, QString type) {
	(void)Plugins::ResolvePluginMedia(
		request.pluginId,
		request.requestId,
		std::move(path),
		std::move(type));
}

void Fail(const Request &request, const QString &why) {
	LOG(("Plugin Media: %1 (%2)").arg(why, request.pluginId));
	if (request.kind == Request::Kind::Get) {
		Resolve(request, QString(), QString());
	}
}

// Calls `check` after every finished download until it returns true.
void WaitDownloads(
		not_null<Main::Session*> session,
		Fn<bool()> check,
		Fn<void()> timeout) {
	if (check()) {
		return;
	}
	const auto lifetime = std::make_shared<rpl::lifetime>();
	session->downloaderTaskFinished(
	) | rpl::on_next([=] {
		if (check()) {
			lifetime->destroy();
		}
	}, *lifetime);
	base::call_delayed(kDownloadTimeout, session, [=] {
		if (*lifetime) {
			lifetime->destroy();
			timeout();
		}
	});
}

void GetPhoto(
		not_null<Main::Session*> session,
		const Request &request,
		not_null<HistoryItem*> item,
		not_null<PhotoData*> photo) {
	const auto view = photo->createMediaView();
	view->wanted(Data::PhotoSize::Large, item->fullId());
	const auto path = MediaFolder() + u"/photo_%1.jpg"_q.arg(photo->id);
	WaitDownloads(session, [=] {
		if (!view->loaded()) {
			return false;
		}
		auto bytes = view->imageBytes(Data::PhotoSize::Large);
		auto file = QFile(path);
		auto saved = false;
		if (!bytes.isEmpty()) {
			saved = file.open(QIODevice::WriteOnly)
				&& file.write(bytes) == bytes.size();
		} else if (const auto image = view->image(Data::PhotoSize::Large)) {
			saved = image->original().save(path, "JPG", 95);
		}
		if (saved) {
			Resolve(request, path, u"photo"_q);
		} else {
			Fail(request, u"could not save photo"_q);
		}
		return true;
	}, [=] { Fail(request, u"photo download timed out"_q); });
}

QString DocumentType(not_null<DocumentData*> document) {
	return document->isVideoMessage() ? u"video_note"_q
		: document->isVoiceMessage() ? u"voice"_q
		: document->sticker() ? u"sticker"_q
		: document->isAnimation() ? u"gif"_q
		: document->isVideoFile() ? u"video"_q
		: document->isAudioFile() ? u"audio"_q
		: u"file"_q;
}

void GetDocument(
		not_null<Main::Session*> session,
		const Request &request,
		not_null<HistoryItem*> item,
		not_null<DocumentData*> document) {
	const auto type = DocumentType(document);
	if (const auto existing = document->filepath(true); !existing.isEmpty()) {
		Resolve(request, existing, type);
		return;
	}
	if (document->size > kMaxDocumentSize) {
		Fail(request, u"file is too large"_q);
		return;
	}
	auto name = QFileInfo(document->filename()).fileName();
	if (name.isEmpty()) {
		const auto mime = QMimeDatabase().mimeTypeForName(document->mimeString());
		name = u"file"_q + (mime.preferredSuffix().isEmpty()
			? QString()
			: (u"."_q + mime.preferredSuffix()));
	}
	const auto path = MediaFolder() + u"/%1_%2"_q.arg(document->id).arg(name);
	const auto view = document->createMediaView();
	document->save(item->fullId(), path);
	WaitDownloads(session, [=] {
		(void)view;
		if (document->loading()) {
			return false;
		}
		const auto ready = document->filepath(true);
		if (!ready.isEmpty()) {
			Resolve(request, ready, type);
		} else {
			Fail(request, u"download failed"_q);
		}
		return true;
	}, [=] {
		document->cancel();
		Fail(request, u"download timed out"_q);
	});
}

void Get(not_null<Main::Session*> session, const Request &request) {
	const auto peer = Plugins::PeerFromDialogId(request.chatId);
	const auto item = session->data().message(peer, MsgId(request.messageId));
	const auto media = item ? item->media() : nullptr;
	if (!media) {
		Fail(request, u"no media in message"_q);
	} else if (const auto photo = media->photo()) {
		GetPhoto(session, request, item, photo);
	} else if (const auto document = media->document()) {
		GetDocument(session, request, item, document);
	} else {
		Fail(request, u"unsupported media"_q);
	}
}

void Send(not_null<Main::Session*> session, const Request &request) {
	const auto peer = session->data().peerLoaded(
		Plugins::PeerFromDialogId(request.chatId));
	if (!peer) {
		Fail(request, u"unknown chat"_q);
		return;
	}
	const auto right = (request.kind == Request::Kind::Photo)
		? ChatRestriction::SendPhotos
		: (request.kind == Request::Kind::VideoNote)
		? ChatRestriction::SendVideoMessages
		: ChatRestriction::SendFiles;
	if (!Data::CanSend(peer, right)) {
		Fail(request, u"no right to send this media here"_q);
		return;
	}
	auto action = Api::SendAction(session->data().history(peer));
	if (request.replyChatId && request.replyMessageId) {
		action.replyTo = FullReplyTo{
			.messageId = FullMsgId(
				Plugins::PeerFromDialogId(*request.replyChatId),
				MsgId(*request.replyMessageId)),
		};
	}
	if (request.kind == Request::Kind::VideoNote) {
		auto file = QFile(request.path);
		if (file.size() > kMaxVideoNoteSize || !file.open(QIODevice::ReadOnly)) {
			Fail(request, u"cannot read video note"_q);
			return;
		}
		const auto information = FileLoadTask::ReadMediaInformation(
			request.path,
			QByteArray(),
			QMimeDatabase().mimeTypeForFile(request.path).name());
		const auto video = std::get_if<Ui::PreparedFileInformation::Video>(
			&information->media);
		if (!video || video->duration <= 0) {
			Fail(request, u"video note is not a video"_q);
			return;
		}
		session->api().sendVoiceMessage(
			file.readAll(),
			VoiceWaveform(),
			video->duration,
			true,
			action);
		return;
	}
	auto list = Storage::PrepareMediaList(
		QStringList(request.path),
		st::sendMediaPreviewSize,
		session->premium());
	if (list.error != Ui::PreparedList::Error::None || list.files.empty()) {
		Fail(request, u"cannot prepare file"_q);
		return;
	}
	list.files.front().caption = TextWithTags{ request.caption };
	const auto type = (request.kind == Request::Kind::Photo)
		? SendMediaType::Photo
		: SendMediaType::File;
	session->api().sendFiles(std::move(list), type, nullptr, action);
}

} // namespace

void HandlePluginMedia(Plugins::MediaRequest request) {
	const auto session = ActiveSession();
	if (!session) {
		Fail(request, u"no account"_q);
	} else if (request.kind == Request::Kind::Get) {
		Get(session, request);
	} else {
		Send(session, request);
	}
}

}

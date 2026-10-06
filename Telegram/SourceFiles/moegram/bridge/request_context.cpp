#include "moegram/bridge/requests.h"

#include "core/application.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"

namespace MoeGram::Bridge {

std::optional<Plugins::RequestEvent> PrepareRequestHooks(
		MTP::Instance *instance,
		MTP::details::SerializedRequest &request,
		MTP::ResponseHandler &callbacks) {
	auto snapshot = Plugins::RequestHooksEnabled()
		? Plugins::RequestSnapshot(0, request) : std::nullopt;
	if (snapshot) {
		for (const auto &entry : Core::App().domain().accounts()) {
			const auto current = entry.account->maybeSession();
			if (current && &entry.account->mtp() == instance) {
				snapshot->accountId = current->uniqueId();
				break;
			}
		}
		if (!snapshot->accountId) {
			snapshot.reset();
		}
	}
	return PrepareRequestHooks(std::move(snapshot), request, callbacks);
}

}

#include "moegram/bridge/requests.h"

namespace MoeGram::Bridge {

std::optional<Plugins::RequestEvent> PrepareRequestHooks(
		std::optional<Plugins::RequestEvent> snapshot,
		MTP::details::SerializedRequest &request,
		MTP::ResponseHandler &callbacks) {
	if (!snapshot) {
		return std::nullopt;
	}
	using MTP::details::SerializedRequest;
	auto detached = SerializedRequest::Prepare(
		request->size() - SerializedRequest::kMessageBodyPosition);
	*detached = *request;
	request = std::move(detached);
	const auto event = *snapshot;
	callbacks.done = [event, done = std::move(callbacks.done)](
			const MTP::Response &response) mutable {
		Plugins::ObserveRequestResponse(event, response);
		return done ? done(response) : true;
	};
	callbacks.fail = [event, fail = std::move(callbacks.fail)](
			const MTP::Error &error, const MTP::Response &response) {
		if (error.type() != u"PLUGIN_REQUEST_CANCELLED"_q) {
			Plugins::ObserveRequestError(event, error);
		}
		return fail ? fail(error, response) : true;
	};
	return snapshot;
}

void DispatchRequest(
		Plugins::RequestEvent snapshot,
		MTP::details::SerializedRequest request,
		RequestValidity valid,
		RequestSend send,
		RequestCancel cancel) {
	Plugins::FilterRequest(std::move(snapshot),
		[request = std::move(request), valid = std::move(valid),
			send = std::move(send), cancel = std::move(cancel)](
				std::optional<Plugins::RequestEvent> filtered) mutable {
			if (!valid()) {
				return;
			}
			if (!filtered) {
				auto response = MTP::Response();
				response.requestId = request->requestId;
				MTPRpcError(MTP_rpc_error(
					MTP_int(400), MTP_string("PLUGIN_REQUEST_CANCELLED")))
					.write(response.reply);
				cancel(response);
				return;
			}
			Plugins::ApplyRequestSnapshot(request, *filtered);
			send(request);
		});
}

}

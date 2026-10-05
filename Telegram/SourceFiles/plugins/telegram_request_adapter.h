#pragma once

#include "plugins/plugin_host.h"
#include "mtproto/details/mtproto_serialized_request.h"
#include "scheme.h"
#include "mtproto/type_utils.h"
#include "mtproto/mtproto_response.h"

namespace MoeGram::Plugins {

[[nodiscard]] std::optional<RequestEvent> RequestSnapshot(
	quint64 accountId, const MTP::details::SerializedRequest &request);
void ApplyRequestSnapshot(
	MTP::details::SerializedRequest &request, const RequestEvent &event);
void ObserveRequestResponse(const RequestEvent &event, const MTP::Response &response);
void ObserveRequestError(const RequestEvent &event, const MTP::Error &error);

}

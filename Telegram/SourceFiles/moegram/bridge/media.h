#pragma once

#include "plugins/plugin_host.h"

namespace MoeGram::Bridge {

// Runs a plugin get_media / send_* request in the active account.
void HandlePluginMedia(Plugins::MediaRequest request);

}

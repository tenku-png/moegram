#pragma once

#include <memory>

namespace Core { class Application; }
namespace MoeGram::Plugins { class Host; }
namespace Window { class SessionController; }

namespace MoeGram::Bridge {

class ApplicationRuntime final {
public:
	explicit ApplicationRuntime(Core::Application *application);
	~ApplicationRuntime();

private:
	std::unique_ptr<Plugins::Host> _host;
	rpl::lifetime _lifetime;

};

// Tells plugins which chat this window shows ("chat_opened").
void TrackActiveChat(not_null<Window::SessionController*> controller);

}

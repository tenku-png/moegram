#pragma once

#include <memory>

namespace Core { class Application; }
namespace MoeGram::Plugins { class Host; }

namespace MoeGram::Bridge {

class ApplicationRuntime final {
public:
	explicit ApplicationRuntime(Core::Application *application);
	~ApplicationRuntime();

private:
	std::unique_ptr<Plugins::Host> _host;

};

}

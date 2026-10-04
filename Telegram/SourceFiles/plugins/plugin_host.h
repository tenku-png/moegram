#pragma once

#include <QtCore/QString>

#include <functional>
#include <memory>

namespace MoeGram::Plugins {

class Host final {
public:
	using Logger = std::function<void(const QString &)>;

	explicit Host(Logger logger);
	~Host();

	Host(const Host &) = delete;
	Host &operator=(const Host &) = delete;

	[[nodiscard]] bool start(const QString &directory);
	void stop();
	[[nodiscard]] int loadedCount() const;

private:
	struct Private;
	const std::unique_ptr<Private> _private;

};

}

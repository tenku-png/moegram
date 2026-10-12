#pragma once

#include "base/basic_types.h"

#include <QtCore/QJsonValue>

namespace MoeGram::Ui {

// Gives a widget a stable name plugins can find ("chat.top_bar").
// Several widgets can share a name (one per window).
void RegisterAnchor(not_null<QWidget*> widget, const QString &name);

// Applies a plugin's description of an anchor, nullopt removes it.
void ApplyAnchor(
	const QString &pluginId,
	const QString &name,
	const std::optional<QJsonValue> &value);

}

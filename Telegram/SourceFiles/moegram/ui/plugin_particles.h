#pragma once

#include "base/basic_types.h"

namespace MoeGram::Ui {

// Plugin "particles" effect over `target` for one area: window, chat or dialogs.
void AttachParticles(not_null<QWidget*> target, const QString &area);

}

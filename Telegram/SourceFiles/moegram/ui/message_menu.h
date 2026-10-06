#pragma once

#include "base/basic_types.h"

class HistoryItem;
namespace Main { class Session; }
namespace Ui { class PopupMenu; }

namespace MoeGram::Ui {

void AddMessageMenuItems(
	not_null<::Ui::PopupMenu*> menu,
	not_null<Main::Session*> session,
	not_null<HistoryItem*> item);

}

#ifndef UI_SETTINGS_USERS_HPP
#define UI_SETTINGS_USERS_HPP

#include <Wt/WStackedWidget>
#include <Wt/WContainerWidget>
#include <Wt/WTable>

#include "common/SessionData.hpp"

namespace UserInterface {
namespace Settings {

class Users : public Wt::WContainerWidget
{
	public:
		Users(SessionData& sessioNData, Wt::WContainerWidget *parent = 0);

		void refresh();

	private:

		void handleDelUser(Wt::WString loginNameIdentity, std::string id);
		void handleEditUser(std::string id);
		void handleAddUser(void);

		SessionData&	_sessionData;

		Wt::WStackedWidget*	_stack;
		Wt::WTable*		_table;
};

} // namespace UserInterface
} // namespace Settings

#endif

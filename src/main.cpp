#include "app-window.h"

#include <iostream>
#include <string>

namespace {

std::string to_std_string(const slint::SharedString &value)
{
    return { value.data(), value.size() };
}

}

int main()
{
    auto app = AppWindow::create();

    app->on_login_clicked([app](slint::SharedString email, slint::SharedString password) {
        const auto email_str = to_std_string(email);
        const auto password_str = to_std_string(password);

        std::cout << "Login clicked\n";
        std::cout << "Email: " << email_str << "\n";
        std::cout << "Password: " << password_str << "\n";

        if (email_str.empty() || password_str.empty()) {
            app->set_login_status_text("Please fill in both email and password.");
            return;
        }

        app->set_login_status_text("Login request is ready.");
        app->set_current_page(2);
    });

    app->on_register_clicked([app](slint::SharedString email, slint::SharedString password) {
        const auto email_str = to_std_string(email);
        const auto password_str = to_std_string(password);

        std::cout << "Register clicked\n";
        std::cout << "Email: " << email_str << "\n";
        std::cout << "Password: " << password_str << "\n";

        if (email_str.empty() || password_str.empty()) {
            app->set_register_status_text("Please fill in both email and password.");
            return;
        }

        app->set_register_status_text("Registration request is ready.");
    });

    app->run();
    return 0;
}

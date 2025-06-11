#include "app/App.hpp"
#include <iostream>

extern "C" {
int32_t rusty_extern_c_integer();
}

int main()
{
    if (USE_RUST) { std::cout << "A value given directly by extern c function " << rusty_extern_c_integer() << "\n"; }
    Kataglyphis::App application;
    return application.run();
}

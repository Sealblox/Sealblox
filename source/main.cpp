#include <iostream>
#include <core/Window.hpp>

int main(int, char **)
{
    {
        Window window;
        if (!window.create("Sealblox", 1280, 720))
            return 1;

        while (window.pollEvents())
        {
        }
    }
}

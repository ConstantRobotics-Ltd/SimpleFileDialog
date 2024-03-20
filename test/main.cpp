#include <iostream>
#include <chrono>
#include <thread>
#include "SimpleFileDialog.h"

int main(void)
{
    // Open file dialog.
    std::string file = cr::utils::SimpleFileDialog::dialog();
    std::cout << "File: " << file << std::endl;

    std::this_thread::sleep_for(std::chrono::seconds(5));
    return -1;
}



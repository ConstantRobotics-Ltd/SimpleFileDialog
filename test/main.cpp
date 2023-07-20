#include <iostream>
#include "SimpleFileDialog.h"

int main(void)
{
    // Open file dialog.
    std::string file = cr::utils::SimpleFileDialog::dialog();
    std::cout << "File: " << file << std::endl;

    return -1;
}



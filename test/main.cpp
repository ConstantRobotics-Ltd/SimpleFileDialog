#include <iostream>
#include <string>
#include "SimpleFileDialog.h"
#include "SimpleFileDialogVersion.h"



/// Entry point: shows the file dialog and prints the chosen file.
int main(void)
{
    std::cout << "SimpleFileDialog v" << SIMPLE_FILE_DIALOG_VERSION << std::endl;

    // Open file dialog.
    const std::string file{cr::utils::SimpleFileDialog::dialog()};
    if (file.empty())
    {
        std::cout << "No file selected" << std::endl;
    }
    else
    {
        std::cout << "File: " << file << std::endl;
    }
    return 0;
}

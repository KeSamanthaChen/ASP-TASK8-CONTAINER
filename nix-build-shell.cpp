#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <fstream>
#include <stdlib.h> // execvp
#include <unistd.h> // fork, exec
#include <sys/wait.h> // waitpid

int main(int argc, const char** argv) {
    // std::cout << "Hello from " << argv[0] << ". I got " << argc << " arguments\n" << std::endl;

    // argv[0] argv[1]->build_dir argv[2] ... (command)

    std::stringstream file_path_ss;
    file_path_ss << argv[1] << "/env-vars";
    std::string file_path_string = file_path_ss.str();

    std::ifstream env_vars_file(file_path_string);
    std::string line;
    std::string shell_path;
    while(std::getline(env_vars_file, line)) {
        std::istringstream iss(line);
        std::string declare, x, target;
        if (!(iss >> declare >> x >> target)) {break;}
        if (target.substr(0,6) == "SHELL=") {
            shell_path = target.substr(7, target.length()-8);
            break;
        }
    }

    char **exe_argv = new char*[4];
    exe_argv[0] = new char[shell_path.length()+1];
    std::strcpy(exe_argv[0], shell_path.c_str());
    exe_argv[1] = (char *)"-c";
    std::stringstream argv2;
    argv2 << "source " << file_path_string << "; exec \"$@\" --";
    for (int i=2; i<argc; i++) {
        // check if there is a space
        std::string argv_string = argv[i];
        if (argv_string.find_first_of(" ") != std::string::npos) { // found a space
            argv_string.insert(0, "\'");
            argv_string.append("\'");
        }
        argv2 << " " << argv_string;
    }
    exe_argv[2] = new char[argv2.str().length()+1];
    std::strcpy(exe_argv[2], argv2.str().c_str());
    exe_argv[3] = NULL;

    fprintf(stderr, "test argument: %s\n", exe_argv[2]);

    execvp(exe_argv[0], exe_argv);

}

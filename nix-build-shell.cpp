// https://stackoverflow.com/questions/69153764/unshare-user-namespace-fork-map-uid-then-execvp-failing
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <fstream>
#include <stdlib.h> // execvp
#include <unistd.h> // fork, exec
#include <sys/wait.h> // waitpid
#include <sched.h> //unshare

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

    // getuid, getgid
    int pid = getpid();
    uid_t uid = getuid();
    gid_t gid = getgid();
    // unshare
    if (0!=unshare(CLONE_NEWUSER)) {
        fprintf(stderr, "create new user namespace failed\n");
        exit(1);
    }
    std::string uid_file_path;
    std::string gid_file_path;
    std::string setgroups_file_path;
    uid_file_path.append("/proc/").append(std::to_string(pid)).append("/uid_map");
    gid_file_path.append("/proc/").append(std::to_string(pid)).append("/gid_map");
    setgroups_file_path.append("/proc/").append(std::to_string(pid)).append("/setgroups");

    std::ofstream uid_file(uid_file_path);
    std::ofstream gid_file(gid_file_path);
    std::ofstream setgroups_file(setgroups_file_path);

    if (uid_file.is_open()) {
        uid_file << "1000" << " " << uid << " 1" << std::endl;
        uid_file.close();
    }

    if (setgroups_file.is_open()) {
        setgroups_file << "deny" << std::flush;
        setgroups_file.close();
    }

    if (gid_file.is_open()) {
        gid_file << "100" << " " << gid << " 1" << std::endl;
        gid_file.close();
    }

    // before this command, lots of things need to be done
    execvp(exe_argv[0], exe_argv);

    delete[] exe_argv[0];
    delete[] exe_argv[2];
    delete[] exe_argv;

    return 0;

}

// https://stackoverflow.com/questions/69153764/unshare-user-namespace-fork-map-uid-then-execvp-failing
// https://github.com/NixOS/nix/blob/280543933507839201547f831280faac614d0514/src/libstore/build/local-derivation-goal.cc
#include <iostream>
#include <sstream>
#include <string>
#include <cstring>
#include <fstream>
#include <stdlib.h> // execvp, mkdtemp
#include <unistd.h> // fork, exec, sethostname
#include <sys/wait.h> // waitpid
#include <sys/socket.h> // socket
#include <netinet/in.h> // IPPROTO_IP
#include <sys/ioctl.h>
#include <net/if.h>
#include <sys/mount.h> // mount
#include <sys/stat.h> // mkdir, mknod
#include <fcntl.h> // open
#include <cstdlib> // system
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
    exe_argv[0] = new char[shell_path.length()+1]; // shell path
    std::strcpy(exe_argv[0], shell_path.c_str());
    exe_argv[1] = (char *)"-c";
    std::stringstream argv2;
    // need change to "build" maybe
    // argv2 << "source " << file_path_string << "; exec \"$@\" --";
    argv2 << "source /build/env-vars; exec \"$@\" --";
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

    // creating a temporary directory "mkdtemp"
    // build directory of a failing nix build -> temporary directory
    char tmp_template[] = "/tmp/directory-XXXXXX";
    char* chroot_dir = mkdtemp(tmp_template);
    std::string chroot_dir_string = chroot_dir;
    fprintf(stderr, "new tmp directory address: %s\n", chroot_dir);

    // create a build directory
    // append have problem like that
    mkdir((chroot_dir_string + "/build").c_str(), 0777);
    // copy the file
    std::string cp_string = "cp -a ";
    cp_string.append(argv[1]).append("/* ").append(chroot_dir_string).append("/build");
    std::system(cp_string.c_str());
    fprintf(stderr, "cp success: %s\n", cp_string.c_str());

    // getuid, getgid
    int pid = getpid();
    uid_t uid = getuid();
    gid_t gid = getgid();
    // unshare CLONE_NEWUSER CLONE_NEWUTS CLONE_NEWNET
    if (0!=unshare(CLONE_NEWUSER | CLONE_NEWUTS | CLONE_NEWNET)) {
        fprintf(stderr, "create new namespaces failed\n");
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

    sethostname((char *)"localhost", 9);
    setdomainname((char *)"(none)", 6);

    int sockfd = socket(PF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sockfd == -1) {
        fprintf(stderr, "open ip socket failed\n");
    }
    struct ifreq ifr;
    strcpy(ifr.ifr_name, "lo");
    ifr.ifr_flags = IFF_UP | IFF_LOOPBACK | IFF_RUNNING;
    if (ioctl(sockfd, SIOCSIFFLAGS, &ifr) == -1) {
        fprintf(stderr, "set loopback interface failed\n");
    }

    // make sure all mounts are mounted as private
    mount(0, "/", 0, MS_PRIVATE | MS_REC, 0);

    // the build box should only contain the following top level directories
    mkdir((chroot_dir_string + "/nix").c_str(), 0777);

    mkdir((chroot_dir_string + "/bin").c_str(), 0777);
    mkdir((chroot_dir_string + "/etc").c_str(), 0777);
    mkdir((chroot_dir_string + "/dev").c_str(), 0777);
    // chmod, but already 0777
    mkdir((chroot_dir_string + "/tmp").c_str(), 0777);
    // chmod((chroot_dir_string + "/tmp").c_str(), 01777);

    mkdir((chroot_dir_string + "/proc").c_str(), 0777);

    // mkdir for all the subdirectory below
    // they are files?? mknod (EPREM) OpenOptions creating the files
    // do I need to close them??
    open((chroot_dir_string + "/dev/full").c_str(), O_CREAT, 0777);
    open((chroot_dir_string + "/dev/kvm").c_str(), O_CREAT, 0777);
    open((chroot_dir_string + "/dev/null").c_str(), O_CREAT, 0777);
    open((chroot_dir_string + "/dev/random").c_str(), O_CREAT, 0777);
    open((chroot_dir_string + "/dev/tty").c_str(), O_CREAT, 0777);
    open((chroot_dir_string + "/dev/urandom").c_str(), O_CREAT, 0777);
    open((chroot_dir_string + "/dev/zero").c_str(), O_CREAT, 0777);
    open((chroot_dir_string + "/dev/ptmx").c_str(), O_CREAT, 0777);
    open((chroot_dir_string + "/bin/sh").c_str(), O_CREAT, 0777);

    // directory
    mkdir((chroot_dir_string + "/dev/pts").c_str(), 0777);
    // directory
    mkdir((chroot_dir_string + "/dev/shm").c_str(), 0777);

    // bind mount the /nix directory MS_BIND
    // https://man7.org/linux/man-pages/man2/mount.2.html
    //    SOURCE   TARGET
    mount("/nix", (chroot_dir_string + "/nix").c_str(), "", MS_BIND | MS_REC, 0); // need MS_REC or not?
    // mount the /dev related
    mount("/dev/full", (chroot_dir_string + "/dev/full").c_str(), "", MS_BIND | MS_REC, 0);
    // leave the /dev/kvm for now
    mount("/dev/kvm", (chroot_dir_string + "/dev/kvm").c_str(), "", MS_BIND | MS_REC, 0);
    mount("/dev/null", (chroot_dir_string + "/dev/null").c_str(), "", MS_BIND | MS_REC, 0);
    mount("/dev/random", (chroot_dir_string + "/dev/random").c_str(), "", MS_BIND | MS_REC, 0);
    mount("/dev/tty", (chroot_dir_string + "/dev/tty").c_str(), "", MS_BIND | MS_REC, 0);
    mount("/dev/urandom", (chroot_dir_string + "/dev/urandom").c_str(), "", MS_BIND | MS_REC, 0);
    mount("/dev/zero", (chroot_dir_string + "/dev/zero").c_str(), "", MS_BIND | MS_REC, 0);
    mount("/dev/ptmx", (chroot_dir_string + "/dev/ptmx").c_str(), "", MS_BIND | MS_REC, 0);
    // bind mount
    mount("/dev/pts", (chroot_dir_string + "/dev/pts").c_str(), "", MS_BIND | MS_REC, 0);
    // mount a tempfs to this directory, and make it read/write/executable for all users/groups
    // chmod? check the source code, problem here
    mount("none", (chroot_dir_string + "/dev/shm").c_str(), "tmpfs", 0, 0);
    // bind mount the shell path parsed in test 1 from env-vars to /bin/sh in the sandbox
    // file, not a directory, problem here
    mount(shell_path.c_str(), (chroot_dir_string + "/bin/sh").c_str(), "", MS_BIND | MS_REC, 0);

    // create the sym_link
    symlink("/proc/self/fd", (chroot_dir_string + "/dev/fd").c_str());
    symlink("/proc/self/fd/0", (chroot_dir_string + "/dev/stdin").c_str());
    symlink("/proc/self/fd/1", (chroot_dir_string + "/dev/stdout").c_str());
    symlink("/proc/self/fd/2", (chroot_dir_string + "/dev/stderr").c_str());

    // // write to /etc file
    open((chroot_dir_string + "/etc/group").c_str(), O_CREAT, 0777);
    open((chroot_dir_string + "/etc/passwd").c_str(), O_CREAT, 0777);
    open((chroot_dir_string + "/etc/hosts").c_str(), O_CREAT, 0777);

    std::ofstream etc_group_file(chroot_dir_string + "/etc/group");
    std::ofstream etc_passwd_file(chroot_dir_string + "/etc/passwd");
    std::ofstream etc_hosts_file(chroot_dir_string + "/etc/hosts");

    if (etc_group_file.is_open()) {
        etc_group_file << "root:x:0:" << std::endl;
        etc_group_file << "nixbld:!:100:" << std::endl;
        etc_group_file << "nogroup:x:65534:" << std::endl;
        etc_group_file.close();
    }

    if (etc_passwd_file.is_open()) {
        etc_passwd_file << "root:x:0:0:Nix build user:/build:/noshell" << std::endl;
        etc_passwd_file << "nixbld:x:1000:100:Nix build user:/build:/noshell" << std::endl;
        etc_passwd_file << "nobody:x:65534:65534:Nobody:/:/noshell" << std::endl;
        etc_passwd_file.close();
    }

    if (etc_hosts_file.is_open()) {
        etc_hosts_file << "127.0.0.1 localhost" << std::endl;
        etc_hosts_file << "::1 localhost" << std::endl;
    }

    // mount new instance of procfs to /proc, this step should after "forking into a child process" for PID namespace
    // as an unpriviledge user, need PID namespaces&mount namespaces
    // EINVAL for wrong flags
    // mount("none", (chroot_dir_string + "/proc").c_str(), "proc", 0, 0);
    mount("/proc", (chroot_dir_string + "/proc").c_str(), "", MS_BIND | MS_REC, 0);


    // unshare the mount
    unshare(CLONE_NEWNS);
    // chroot
    chdir(chroot_dir);
    chroot(".");

    // before this command, lots of things need to be done
    // the path need to change to /build/env-vars
    execvp(exe_argv[0], exe_argv);

    delete[] exe_argv[0];
    delete[] exe_argv[2];
    delete[] exe_argv;

    return 0;

}

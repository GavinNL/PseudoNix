#include <PseudoNix/System.h>
#include <PseudoNix/Shell.h>
#include <PseudoNix/HostMount.h>

#if !defined __EMSCRIPTEN__
#include <PseudoNix/ArchiveMount.h>
#include <PseudoNix/sample_archive.h>
#endif

inline void setup_functions(PseudoNix::System & sys)
{
    // The sh function is provided for you.
    // It's relatively rudametry but allowed you do
    // do simple linux pipling and shell substitution
    //
    // cmd1 | cmd2
    // cmd1 && cmd2
    // cmd1 || cmd2
    // echo "Hello ${USER}"
    //
    PseudoNix::enable_default_shell(sys);
    PseudoNix::enable_host_mount(sys);    // lets you moung host file systems

#if !defined __EMSCRIPTEN__
    PseudoNix::enable_archive_mount(sys); // lets you mount tar/tar.gz files
#endif

    // Here's a very simple guessing game process
    sys.setFunction("guess", "A simple guessing game", [](PseudoNix::System::e_type ctrl) -> PseudoNix::System::task_type
                  {
                      // Macro to define a few variables such as
                      // IN, OUT, ENV, SYSTEM, ARGS, PID
                      PN_PROC_START(ctrl);

                      std::string input;
                      uint32_t random_number = static_cast<uint32_t>(std::rand() % 100) + 1u;
                      COUT << std::format("I have chosen a number between 1-100. Can you guess what it is?\n");

                      while(true)
                      {
                          std::string line;

                          // PN_HANDLE_AWAIT_INT_TERM is a macro that looks at the return type of the
                          // Awaiter (a signal code), and co_returns the appropriate
                          // exit code. It will exit if the code is SIG_TERM or SIG_INT
                          //
                          // This is where Ctrl-C and Sig-kills are handled
                          PN_HANDLE_AWAIT_INT_TERM(co_await ctrl->await_read_line(ctrl->in, line), ctrl)

                          uint32_t guess = 0;

                          if(std::errc() != std::from_chars(line.data(), line.data() + line.size(), guess).ec)
                          {
                              COUT << std::format("invalid entry: {}\n", line);
                              COUT << std::format("Guess Again: \n");
                              continue;
                          }

                          if(guess > random_number)
                          {
                              COUT << std::format("Too High!\n");
                          }
                          else if(guess < random_number)
                          {
                              COUT  << std::format("Too Low!\n");
                          }
                          else
                          {
                              COUT << std::format("Awesome! You guessed the correct number: {}!\n", random_number);
                              COUT << std::format("Exiting\n");
                              co_return 0;
                          }
                      }

                      co_return 0;
                  });

    // Create the default bob user
    sys.userCreate(1, "bob");

    // This is the pre-exec function that gets called
    // right before the coroutine is first executed
    //
    // It is used to modify the arguments
    //
    // You can use it to modify the args or add new
    // data such as environment variables
    sys.m_preExec = [](PseudoNix::System::Exec &E) {
        //E.env["USER"] = "bob";
        E.env["PSEUDONIX_VERSION"] = std::format("{}.{}", PSEUDONIX_VERSION_MAJOR, PSEUDONIX_VERSION_MINOR);
#if !defined __EMSCRIPTEN__
#if defined CMAKE_SOURCE_DIR
        E.env["CMAKE_SOURCE_DIR"] = CMAKE_SOURCE_DIR;
        E.env["CMAKE_BINARY_DIR"] = CMAKE_BINARY_DIR;
#endif
#endif
        E.env["COMPILE_DATE"]     = std::format("{} {}", __DATE__, __TIME__);
    };

    sys.mkdir("/bin");
    sys.mkfile("/bin/hello.sh");
    sys.fs("/bin/hello.sh") <<
        R"foo(
echo Arguments: ${1} ${2} ${3} ${4}
echo "this is a script defined inside the virtual file system"
echo "I'm going to sleep now for a few seconds"
sleep 3
echo "Hey! I'm awake!"
sleep 1
echo "Hey! I'm awake!"
)foo";


    // Create the /etc/profile file so that
    // every instance of the sh command will execute
    // those commands first
    sys.mkdir("/etc");
    sys.mkfile("/etc/profile");
    sys.fs("/etc/profile") << R"foo(
export PATH=/usr/bin:/bin
echo "###################################"
echo "Welcome to the shell!"
echo " "
echo "The shell process automatically sources the"
echo "/etc/profile in the Virtual File System"
echo " "
echo "You are user: ${USER}"
echo "This is SHELL_PID: ${SHELL_PID}"
echo "Compiled Date: ${COMPILE_DATE}"
echo " "
echo "/bin contains in-memory scripts"
echo "/etc contains the profile that sh reads"
echo "/usr/bin a mounted directory"
echo " "
echo "type 'help' for a list of commands"
echo "###################################"
)foo";

    sys.mkdir("/mnt");
    sys.mkfile("/mnt/README.md");
    sys.fs("/mnt/README.md") <<
R"foo(
/mnt/ar_app - The actual archive data is embedded in
              application memory. Unmounting
              this folder us not undoable.

/mnt/ar_vfs  - /share/archive.tar.gz exists in the
               virtual file system. It is mounted
               at this location. You can unmount
               this and remount using the following:

               umount  /mnt/ar_vfs
               mount archive /share/archive.tar.gz /mnt/ar_vfs
)foo";

#if !defined __EMSCRIPTEN__
    {
        // Create a virtual data file of the tar.gz data
        // and place it in /share/archive.tar.gz
        //
        sys.mkdir("/share");
        sys.mkfile("/share/archive.tar.gz");
        sys.fs("/share/archive.tar.gz") << PseudoNix::archive_tar_gz;

        // Mount that virtual archive at /mnt/ar_vfs
        sys.mkdir("/mnt/ar_vfs");
        sys.spawnProcess({"mount", "archive", "/share/archive.tar.gz", "/mnt/ar_vfs"});
    }
#endif

#if !defined __EMSCRIPTEN__
    {
        // The data for the archive is a available at compile time
        // mount the raw data directly at /mnt/ar_app
        //
        sys.mkdir("/mnt/ar_app");
        if(PseudoNix::FSResult::True != sys.mount<PseudoNix::ArchiveMount>("/mnt/ar_app",
                                                                             static_cast<void*>(PseudoNix::archive_tar_gz.data()),
                                                                             static_cast<size_t>(PseudoNix::archive_tar_gz.size())))
        {
            std::cerr << "Failed to load the tar.gz from memory" << std::endl;
        }
    }

#endif



    // Create a new task queue called "THREAD"
    // This can be executed at a different
    // time as the MAIN task queue.
    sys.taskQueueCreate("PRE_MAIN");
    sys.taskQueueCreate("POST_MAIN");
    sys.taskQueueCreate("THREADPOOL");
}

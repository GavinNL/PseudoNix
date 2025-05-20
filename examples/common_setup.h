#include <PseudoNix/System.h>
#include <PseudoNix/Shell.h>
#include <PseudoNix/ArchiveMount.h>

#include <PseudoNix/sample_archive.h>

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
    sys.setFunction("sh", "Default Shell", PseudoNix::shell_coro);

    // Here's a very simple guessing game process
    sys.setFunction("guess", "A simple guessing game", [](PseudoNix::System::e_type ctrl) -> PseudoNix::System::task_type
                  {
                      // Macro to define a few variables such as
                      // IN, OUT, ENV, SYSTEM, ARGS, PID
                      PSEUDONIX_PROC_START(ctrl);

                      std::string input;
                      uint32_t random_number = std::rand() % 100 + 1;
                      COUT << std::format("I have chosen a number between 1-100. Can you guess what it is?\n");

                      while(true)
                      {
                          std::string line;

                          // HANDLE_AWAIT_INT_TERM is a macro that looks at the return type of the
                          // Awaiter (a signal code), and co_returns the appropriate
                          // exit code. It will exit if the code is SIG_TERM or SIG_INT
                          //
                          // This is where Ctrl-C and Sig-kills are handled
                          HANDLE_AWAIT_INT_TERM(co_await ctrl->await_read_line(ctrl->in, line), ctrl)

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

    // This is the pre-exec function that gets called
    // right before the coroutine is first executed
    //
    // It is used to modify the arguments
    //
    // You can use it to modify the args or add new
    // data such as environment variables
    sys.m_preExec = [](PseudoNix::System::Exec & E)
    {
        E.env["USER"] = "bob";
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
    sys.touch("/bin/hello.sh");
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
    sys.touch("/etc/profile");
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

    if(PseudoNix::FSResult::Success != sys.mount2_t<PseudoNix::ArchiveNodeMount>("/mnt", archive_tar_gz, archive_tar_gz_len))
    {
        std::cerr << "Failed to load the tar.gz from memory" << std::endl;
    }

    // Create a new task queue called "THREAD"
    // This can be executed at a different
    // time as the MAIN task queue.
    sys.taskQueueCreate("PRE_MAIN");
    sys.taskQueueCreate("POST_MAIN");
    sys.taskQueueCreate("THREADPOOL");
}

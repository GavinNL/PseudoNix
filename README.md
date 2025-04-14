# PseudoNix

PseudoNix is an embeddable, Linux-like environment you can integrate directly into your project to provide an interactive shell interface. 
It supports custom command registration and mimics familiar shell behavior including environment variables, 
command substitution, logical operators (&&, ||), and output redirection (|). 
Perfect for building scriptable, extensible CLI experiences right into your application.

Although PseudoNix can be used for command line applications, 
it was intended to be used for interactive applications, like games, to provide a debugging interface for 
your system.

PseudoNix provides a default `shell` that behaves similar to bash. 
Through this interactive shell, you have access to a number of bash-like features:

* Execute a process: 
  * `echo hello world`
* Execute a compound process: 
  * `sleep 5 && echo hello world`
  * `true && echo true || echo false`
  * `false && echo true || echo false`
* Run a process with additional variables: 
  * `VAR=value env`
* Set variables for the shell 
  * `VAR=VALUE`
* Use variables in the shell:
  * `echo hello ${VAR}`
* Export variables to child processes: 
  * `export VAR`
* Execute a process in the background: 
  * `sleep 10 && echo hello world &`
* Use Command Substituion
  * `echo Running for: $(uptime) ms`



PseudoNix allows you to register your own coroutine functions so that they can be called within the system.

## Compiling the Examples

Edit the top level `CMakeLists.txt` and change the project name. 

```bash
cd SRC_FOLDER

# execute conan to install the packages you need
conan install conanfile.py --build missing -of=build

# Run cmake
cmake --preset conan-release .

```

## Usage

This is a slightly stripped down version of the `main.cpp` example.

```c++
#include <PseudoNix/MiniLinux.h>
#include <PseudoNix/shell2.h>
#include <PseudoNix/Launcher.h>

int main()
{
    // Create a default PseudoNix system which comes
    // with a few standard functions
    PseudoNix::System M;

    // Set the default shell. See main.cpp for
    // a more detailed explaination
    M.setFunction("sh", std::bind(PseudoNix::shell_coro, std::placeholders::_1, PseudoNix::ShellEnv{}));

    // Set the launcher function which reads data from
    // standard input and redirects it to a sub process
    M.setFunction("launcher", PseudoNix::launcher_coro);

    // Execute the launcher proces
    auto pid = M.runRawCommand(System::parseArguments({"launcher", "sh"}));

    // Execute all the processes
    // keep looping until all processes have completed
    while(M.executeAllFor(std::chrono::milliseconds(1), 10))
    {
        // sleep for 1 millisecond so we're not
        // doing a busy loop
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;

}

```

## Default Function Processes

| name     | Description                                                       |
| -------- | ----------------------------------------------------------------- |
| echo     | echos arguments to output: `echo hello world`                     |
| env      | Shows all current env variables                                   |
| exit     | Exits the shell                                                   |
| export   | Exports env variables to children                                 |
| exported | Lists all exported variables                                      |
| false    | Returns immediately with exit code 1                              |
| true     | Returns immediately with exit code 0                              |
| help     | Lists all commands available                                      |
| kill     | Kill a process: `kill <PID>`                                      |
| launcher | Launches another process and redirects stdin/out to the process   |
| ps       | Lists all processes                                               |
| rev      | Reverses the input characters                                     |
| sh       | The main shell process                                            |
| signal   | Signal a process `signal <pid> <signal>`                          |
| sleep    | Pause for a few seconds                                           |
| uptime   | Prints milliseconds since start up                                |
| wc       | Counts the characters in input                                    |
| yes      | Outputs 'y' to the output                                         |

## Example Custom Function

Here's an example of creating a simple guessing game within the PsuedoNix system. 
Remember that all the process functions happen concurrently,  so the

 * All process functions happen concurrently, but on the main thread.
 * Long running processes will block your entire application
 * Make sure to use awaiters to yield the process flow to the next scheduled process

```c++

int main()
{
    PseudoNix::System M;

    M.setFunction("guess", [](PseudoNix::System::e_type ctrl) -> PseudoNix::System::task_type
    {
        std::string input;
        uint32_t random_number = std::rand() % 100 + 1;
        *ctrl->out << std::format("I have chosen a number between 1-100. Can you guess what it is?\n");

        while(true)
        {
            std::string line;

            // HANDLE_AWAIT is a macro that looks at the return type of the
            // Awaiter (a signal code), and co_returns the appropriate
            // exit code.
            //
            // This is where Ctrl-C and Sig-kills are handled
            HANDLE_AWAIT(co_await ctrl->await_read_line(ctrl->in.get(), line))

            uint32_t guess = 0;

            if(std::errc() != std::from_chars(line.data(), line.data() + line.size(), guess).ec)
            {
                *ctrl->out << std::format("invalid entry: {}\n", line);
                *ctrl->out << std::format("Guess Again: \n");
                continue;
            }

            if(guess > random_number)
            {
                *ctrl->out << std::format("Too High!\n");
            }
            else if(guess < random_number)
            {
                *ctrl->out << std::format("Too Low!\n");
            }
            else
            {
                *ctrl->out << std::format("Awesome! You guessed the correct number: {}!\n", random_number);
                *ctrl->out << std::format("Exiting\n");
                co_return 0;
            }
        }

        co_return 0;
    });
}
```
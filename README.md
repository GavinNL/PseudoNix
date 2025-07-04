# PseudoNix

<img src="https://img.shields.io/github/actions/workflow/status/GavinNL/PseudoNix/.github%2Fworkflows%2Fcmake-multi-platform.yml?branch=main&style=for-the-badge&logo=github&label=main" 
href="https://github.com/GavinNL/PseudoNix/actions?query=branch%3Amain" alt="Main GitHub Actions Workflow Status"> <img src="https://img.shields.io/github/actions/workflow/status/GavinNL/PseudoNix/.github%2Fworkflows%2Fcmake-multi-platform.yml?branch=dev&style=for-the-badge&logo=github&label=dev" 
href="https://github.com/GavinNL/PseudoNix/actions?query=branch%3Adev"  alt="Dev GitHub Actions Workflow Status">

PseudoNix is an embeddable header-only, Linux-like environment you can integrate
directly into your project to provide concurrent process like behaviour.

[Live Demo Using ImGui](https://filedn.eu/l0rnKqYfU3SSI61WTa9844f/PseudoNix/index.html)

## Dependendices

* **Required**
  * C++20 Compiler
  * [readerwriterqueue](https://github.com/cameron314/readerwriterqueue) by cameron314 (available on Conan)
  * [concurrentqueue](https://github.com/cameron314/concurrentqueue) by cameron314 (available on Conan)
* **Optional**
  * [libarchive](https://github.com/libarchive/libarchive) (available on Conan) - required to mount tar/tar.gz files

## Compiling the Examples

Edit the top level `CMakeLists.txt` and change the project name. 

```bash
cd SRC_FOLDER

# execute conan to install the packages you need
conan install conanfile.py --build missing -of=build

# Run cmake
cmake --preset conan-release .

```

## Usage In your Project

If you are using the Conan Package Manager, you can add the following to your dependences list:

```python
    self.requires("readerwriterqueue/1.0.6")
    self.requires("concurrentqueue/1.0.4")

    # Optional: Allows mounting tar/tar.gz files
    self.requires("libarchive/3.7.9")

    # Optional: Provides a working GUI terminal emulator
    #           for Imgui applications
    self.requires("imgui/1.91.8-docking")
```

Add this repo as a submodule and then add it as a subdirectory

```cmake
find_package(readerwriterqueue REQUIRED)
find_package(concurrentqueue REQUIRED)

add_subdirectory(third_party/PseudoNix)

target_link_libraires(myapp PseudoNix::PseudoNix readerwriterqueue::readerwriterqueue concurrentqueue::concurrentqueue)
```

## How It Works

The PseudoNix::System acts like a fully contained Linux system and scheduler
which can execute processes concurrently. A process is a coroutine which can be
added to the system to be executed. The coroutines that are added to the system
are not automatically executed. When `system.executeAll()` is called, each
coroutine is resumed one at a time until all of them have been resumed.

Coroutines run on a single thread by design in the order of their PID number.

The coroutines provide a `input/output` stream which can be written to. This is
simlar to the standard input/output streams, but instead of writing to the
console, it writes to memory. This way the output of one process can be sent to
the input of another, just like on Linux.

## Use Cases

This library was built because I needed a way to interface with a custom CAD
application I was building. I needed a shell interface where I could execute
commands and probe information about the system. After add more and more
features, I decided to turn it into its own library.

### Features

 - Bash-like shell interface
 - Simple shell scripts
 - Define your own process coroutines similar to a linux process
 - Run your processes within the system's scheduler
 - Chain processes together `proc1 | proc2` just like in Linux
 - Signal running proccess to terminate using the `kill` command
 - ImGui Terminal Window
 - Thread Pools to run processes outside of the MAIN Task Queue

### Future Development

 - [x] Virtual Filesystem
   - [x] Mounting Archives
   - [x] Mounting Archives from Memory
 - [x] Better bash-features (if statements, loops)
   - [x] If-statements
   - [x] While-Loops
   - [x] For-Loops
   - [x] breaks/continue
 - [ ] Pausing processes
 - [ ] More GNU core-utils like functions
   - [ ] head/tail
   - [ ] grep


## Examples

### Example 1: Basic Usage

```c++
#include <PseudoNix/System.h>


PseudoNix::System::task_type my_custom_function(PseudoNix::System::e_type ctrl)
{
    auto sleep_time = std::chrono::milliseconds(250);
    for(int i=0;i<10;i++)
    {
        std::cout << std::format("[{}] Counter: {}", ctrl->args[1], i) << std::endl;

        // yield some time back to the scheduler
        // so that other processes can execute
        co_await ctrl->await_yield_for(sleep_time);
    }
    co_return 0;
}

int main()
{
    using namespace PseudoNix;
    // The first thing we need to do is create
    // the instance of the mini linux system
    //
    System M;
    // add our coroutine to the list of functions to be
    // called
    M.setFunction("mycustomfunction", my_custom_function);

    // run 3 instances of the coroutine using different input
    // arguments
    M.spawnProcess({"mycustomfunction", "alice"});
    M.spawnProcess({"mycustomfunction", "bob"});
    M.spawnProcess({"mycustomfunction", "charlie"});

    // Execute the main task queue
    while(M.taskQueueExecute())
    {
        // sleep for 1 millisecond so we're not
        // doing a busy loop
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}

```

### Example 2: Using the Input/Output Streams

This is a slightly stripped down version of the `main.cpp` example. Unlike in
Example 1, where we wrote directly to std::cout, we are instead going to write
to the output stream of the process.

The output stream will be piped into another process which will write the data
to std::cout.

```c++
#include <PseudoNix/System.h>

PseudoNix::System::task_type my_custom_function(PseudoNix::System::e_type ctrl)
{
    auto sleep_time = std::chrono::milliseconds(250);
    for(int i=0;i<10;i++)
    {
        // write to the process's output stream
        *ctrl->out << std::format("[{}] Counter: {}\n", ctrl->args[1], i);
        co_await ctrl->await_yield_for(sleep_time);
    }
    co_return 0;
}

int main()
{
    PseudoNix::System M;

    // add our coroutine to the list of functions to be
    // called
    M.setFunction("mycustomfunction", my_custom_function);

    // We can manually create a pipeline. This will
    // pipe the output of one function into the input of another
    // just like in linux:  mycustomfunction | to_std_cout
    //
    // The to_std_cout function is provided for you
    // It simply takes whatever is in its input buffer
    // and writes it to std::cout
    M.spawnPipelineProcess({
            {"mycustomfunction", "alice"},
            {"to_std_cout"}
    });

    // Execute the main task queue
    while(M.taskQueueExecute())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}
```


### Example 3: Using the Shell Process

A `shell` process, similar to bash, is provided for you. This shell process can
be used to give you an actual command prompt entry into the PseudoNix system and
let you launch commands.

Additionally, if you are building a command line application, you will need the
`launcher` process. The `launcher` reads data from std::cin, and pipes that data
into a new process. It then takes the output and writes that to std::cout.
Without this, the shell will not be able to write anything to your terminal
output.


```c++
#include <PseudoNix/System.h>
#include <PseudoNix/Shell.h>
#include <PseudoNix/Launcher.h>

int main()
{
    PseudoNix::System M;

    // register the shell function
    M.setFunction("sh", std::bind(PseudoNix::shell_coro, std::placeholders::_1, PseudoNix::ShellEnv{}));
    M.setFunction("launcher", PseudoNix::launcher_coro);

    auto launcher_pid = M.spawnProcess({"launcher", "sh"});

    // Execute the main task queue
    while(M.taskQueueExecute())
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    return 0;
}
```

#### Special Shell Features

Psuedonix provides a default **shell** process that can be used as a starting
point to create interactivity. This shell behaves behaves similar to bash,
allowing many features such as:

It supports custom command registration and mimics familiar shell behavior
including environment variables, command substitution, logical operators `&&,
||`, and output redirection `cmd1 | cmd2`. Perfect for building scriptable,
extensible CLI experiences right into your application.

PseudoNix allows you to register your own coroutine functions so that they can
be called within the system.

 * Logical commands: `true && echo true || echo false`
 * Setting environment variables: `VAR=VALUE`
 * Variable substitution: `echo hello ${VAR}`
 * Passing variables to commands: `VAR=value env`
 * Executing in the background: `sleep 10 && echo hello world &`
 * Command substitution: `echo Running for: $(uptime) ms`
 * Call your own coroutine functions

**NOTE**: The `shell` process is not a full bash interpreter. It does not
provide many of the features. It was inteded to be a simple interface into the
PseudoNix system. The following bash features are not provided, but may be
included in the future

  * if statements
  * loops
  * functions


#### Default Functions

Here is a list of commands that are provided by default, mostly for testing purposes.
See the examples below to define your own.

| name           | Description                                                      |
| -------------- | ---------------------------------------------------------------- |
| bgrunner       | Spawn a background thread to process a Task Queue                |
| cat            | Concatenates files to standard output                            |
| cd             | Changes the current working directory                            |
| cp             | Copies files and directories                                     |
| echo           | Prints arguments to standard output                              |
| env            | Prints out all environment variables                             |
| exit           | Exits the shell                                                  |
| export         | Exports environment variables to new processes                   |
| exported       | Prints exported environment variables                            |
| false          | Returns with exit code 1                                         |
| help           | Shows the list of commands                                       |
| io_info        | Shows IO pointers                                                |
| kill           | Terminate a process                                              |
| launcher       | Launches another process and redirects stdin/out to the process. |
| ls             | Lists files and directories                                      |
| mkdir          | Create directories                                               |
| mount          | Mounts host filesystems inside the VFS                           |
| ps             | Shows the current process list                                   |
| pwd            | Prints the current working directory                             |
| queue          | Create/List/Destroy task queues                                  |
| queueHopper    | Example process that hops to different task queues               |
| rev            | Reverses the input                                               |
| rm             | Removes files and directories                                    |
| sh             | The default shell                                                |
| signal         | Send a signal to a process                                       |
| sleep          | Pauses for NUMBER seconds                                        |
| spawn          | Spawns N instances of the same process                           |
| to_std_cout    | Pipes process output to standard output                          |
| touch          | Create files                                                     |
| true           | Returns with exit code 0                                         |
| umount         | Unmounts a host filesystem                                       |
| uptime         | Number of milliseconds since started                             |
| wc             | Counts the number of characters                                  |
| yes            | Keeps printing y to stdout until interrupted                     |

### Example 4: Integrating with GUI

PsuedoNix was originally built to be integrated into a game engine I was building, so
was designed to be easily integrated into a GUI (eg: ImGui)

A very simple ImGui Terminal emulator process has been created for you to use. 

See the [terminal](examples/terminal.cpp) example

```c++
#include <PseudoNix/System.h>
#include <PseudoNix/Shell.h>
#include <PseudoNix/ImGuiTerminal.h>

int main()
{
    PseudoNix::System system;
    system.setFunction("sh", std::bind(PseudoNix::shell_coro, std::placeholders::_1, PseudoNix::ShellEnv{}));
    system.setFunction("term", PseudoNix::terminalWindow_coro);

    # Spawn the Imgui Terminal
    system.spawnProcess({"term", "sh"});

    // somewhere in your imgui draw loop, you can
    // execute the system
    while(true)  {
        ...
        ImGui::BeginFrame();

        M.taskQueueExecute();

        ImGui::EndFrame();
        ...
    }
}
```

### Example 5: Guessing Game

Here's an example of creating a simple guessing game within the PsuedoNix
system. Remember that **all process functions happen concurrently, but on a
single thread**. So to be able to run concurrently, processes that would
normally block at a location, should use specific co-routine awaiters provided by
the ProcessControl object.
 
```c++

int main()
{
    PseudoNix::System M;

    M.setFunction("guess", [](PseudoNix::System::e_type ctrl) -> PseudoNix::System::task_type
    {
        // Macro to define a few variables such as
        // IN, OUT, ENV, SYSTEM, ARGS, PID
        PSEUDONIX_PROC_START(ctrl);

        std::string input;
        uint32_t random_number = std::rand() % 100 + 1;
        OUT << std::format("I have chosen a number between 1-100. Can you guess what it is?\n");

        while(true)
        {
            std::string line;

            // HANDLE_AWAIT_BREAK_ON_SIGNAL is a macro that looks at the return type of the
            // Awaiter (a signal code), and breaks the while loop
            // exit code. It will exit if the code is SIG_TERM or SIG_INT
            //
            // This is where Ctrl-C and Sig-kills are handled
            HANDLE_AWAIT_BREAK_ON_SIGNAL(co_await ctrl->await_read_line(ctrl->in, line), ctrl)

            uint32_t guess = 0;

            if(std::errc() != std::from_chars(line.data(), line.data() + line.size(), guess).ec)
            {
                OUT << std::format("invalid entry: {}\n", line);
                OUT << std::format("Guess Again: \n");
                continue;
            }

            if(guess > random_number)
            {
                OUT << std::format("Too High!\n");
            }
            else if(guess < random_number)
            {
                OUT  << std::format("Too Low!\n");
            }
            else
            {
                OUT << std::format("Awesome! You guessed the correct number: {}!\n", random_number);
                OUT << std::format("Exiting\n");
                co_return 0;
            }
        }

        co_return 0;
    });
}
```

### Example 6: Multiple Task Queues

Processes in the PseudoNix System are executed on a Task Queue. There is a
`"MAIN"` queue which is executed when you call  `system.taskQueueExecute()`. 
By default all tasks will be executed on that queue.

You can create different task queues, which can be executed at different times
in your application. For example, you can have a task queue that executes during
your Physics portion of your game engine, and one that runs during the Render
Pass of your graphics pipeline.

Your processes can switch to different task queues by calling `await_yield` and
passing in the name of the queue you want to continue to execute.

```c++
    co_await ctrl->await_yield("RENDERPASS_QUEUE");
```

Additional Task Queues can be created using the
`system.taskQueueCreate(name_str)` function.

The `queueHopper` function is created by default as an example, but the code is
shown below with some of the validation checks removed.

```c++
int main()
{
    PseudoNix::System M;
    M.setFunction("sh", std::bind(PseudoNix::shell_coro, std::placeholders::_1, ShellEnv{}));
    M.setFunction("launcher", PseudoNix::launcher_coro);

    M.setFunction("queueHopper", [](e_type ctrl) -> task_type
    {
        PSEUDONIX_PROC_START(ctrl);
        using namespace std::chrono_literals;

        std::string TASK_QUEUE_NAME = ARGS[1];

        // the QUEUE variable defined by PSEUDONIX_PROC_START(ctrl)
        // tells you what queue this process is being executed on
        COUT << std::format("On {} queue\n", QUEUE);

        for(int i=0;i<10;i++)
        {
            // Wait 250ms then hop onto the other task queue
            HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield_for(250ms, TASK_QUEUE_NAME), ctrl);

            COUT << std::format("On {} queue\n", QUEUE);

            // hop back onto the default queue
            HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield_for(250ms, PseudoNix::System::DEFAULT_QUEUE), ctrl);

            COUT << std::format("On {} queue\n", QUEUE);
        }

        co_return 0;
    });

    M.taskQueueCreate("PRE_MAIN");
    M.taskQueueCreate("POST_MAIN");

    M.spawnPipelineProcess({
            {"launcher", "sh"}
    });

    while(true)
    {
        // execute each task queue in a specific order
        auto total_tasks =  M.taskQueueExecute("PRE_MAIN");
        total_tasks += M.taskQueueExecute();
        total_tasks += M.taskQueueExecute("POST_MAIN");
        if(total_tasks == 0) 
            break;

        // sleep for 1 millisecond so we're not
        // doing a busy loop
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

}
```


## Signal Handlers, Exiting Gracefully and Traps

In Linux, you can signal a process to interrupt, usually with Ctrl+C. This will
tell the process that it should stop what its doing and react to the event, or
exit the program. To be able to handle this behaviour in your coroutines, when
you co_await, you can use a handy macro `HANDLE_AWAIT_BREAK_ON_SIGNAL` to
read the output of the co_await and break out of the loop if it receives a
signal. 

If you call `signal <PID> 2`, or `signal <PID> 15` from the shell process (the 
2 and the 15 are linux SIG_INT and SIG_TERM), it will tell your coroutine to
exit its while loop. Since we reacted to an interrupt signal, it exited the loop and
exited gracefully, printing out `This is a graceful exit`.

But if we call `kill <PID>`, it will not send a signal, instead if will flag the
coroutine to be removed from the scheduler. You can use the `PSEUDONIX_TRAP`
to create a deferred block that will be executed with the coroutine is destroyed.
This is useful if your coroutine allocated any memory or needs to do some additional
cleanup.

Try running example4.cpp, in the shell process try executing `mycustomfunction` 
in the background. Then use `ps` to find its PID, and either call `signal <PID> 2`
or `kill <PID>`



```c++
PseudoNix::System::task_type mycustomfunction(PseudoNix::System::e_type ctrl)
{
    PSEUDONIX_PROC_START(ctrl);
    auto sleep_time = std::chrono::milliseconds(250);

    PSEUDONIX_TRAP {
        // This will be called even if you call "kill"
        // on the pid
        OUT << std::format("This is executed on cleanup.");
    };

    int i=0;
    while(true)
    {
        OUT << std::format("Counter: {}\n", i++);

        // await for the awaiter to signal
        // if it does, break the while loop if
        // it returned any of the known signals:
        //  sig_terminate, sig_interrupt
        HANDLE_AWAIT_BREAK_ON_SIGNAL(co_await ctrl->await_yield_for(sleep_time), ctrl);
    }

    // this will only be called if the while loop exits
    // properly by reacting to a signal, either:
    // signal PID 2
    // signal PID 15
    OUT << std::format("This a graceful exit\n");
    co_return 0;
}
```

## Process Control 

The ProcessControl object is passed into your function as the input argument. 
These are similar to any linux process. 

You can use the `PSEUDONIX_PROC_START(ctrl)` macro, so define some references
that are easy to access:

```c++
M.setFunction("guess", [](PseudoNix::System::e_type ctrl) -> PseudoNix::System::task_type
{
    PSEUDONIX_PROC_START(ctrl);

    ctrl->args; // vector of strings, your command line arguments
    ctrl->in;  // the standard input stream
    ctrl->out; // the standard output stream
    ctrl->env; // the environment variables

    // COUT - Reference to the output stream
    // CIN  - Reference to the input stream
    // ARGS - The command line arguments to this process
    // ENV  - The environment variable map

    // PID      - the PID number for this process
    // EXPORTED - A map of all exported variables
    // QUEUE    - string indicating the name of the queue that the 
    //            process is running on
    // CWD      - The current working directory of the process

    // SYSTEM           - reference to the pseudonix system/filesystem
    // LAST_SIGNAL      - The last signal that was received
    // PARENT_SHELL_PID - the PID of the parent shell process

    co_return 0;
});
```

## Coroutine Awaiters

The Coroutine Awaiters are used to pause your process and yield the time to
another process in the scheduler until some event has occured.

The following is the simplest awaiter, which just pauses and waits until the
scheduler resumes this process at a later time.

```c++
auto await_result = co_await ctrl->await_yield();
```

The awaiters return an `AwaitResult` enum which tells you what you should do. If
it returns `AwaitResult::SUCCESS`, then the awaiter returned properly.

Other options are `AwaitResult::SIGNAL_INTERRUPT` and
`AwaitResult::SIGNAL_TERMINATE`, which mean the process was asked to interrupt
itself (Ctrl+C in bash), or terminate itself (kill PID).

In most cases, you would want to exit your process, unless you have some custom
behaviour. The `shell` and `launcher` have custom behaviour

```c++
switch(co_await ctrl->await_yield())
{
case AwaiterResult::SIGNAL_INTERRUPT:  { co_return 1;}
case AwaiterResult::SIGNAL_TERMINATE: { co_return 1;}
default: break;
}
```

A macro has been created so that you can do this automatically. The following is
a very simple process that just prints out "hello world" continuously until you
interrupt or kill the process

```c++
M.setFunction("hello", [](PseudoNix::System::e_type ctrl) -> PseudoNix::System::task_type
{
    int i=0;
    while(true)
    {
        *ctrl->out << std::format("Hello world: {}\n", i++);
        HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield(), ctrl);
    }

    co_return 0;
});
```

The following is a list of awaiters that can be used 

| Awaiter                                   | Description
| ----------------------------------------- |------------------------------------------ |
| ctrl->await_yield()                       | Pauses until the next schedule            |
| ctrl->await_yield_for(duration)           | Sleeps for a certain amount of time       |
| ctrl->await_has_data(ctrl->in)            | Waits until there is data in the stream   |
| ctrl->await_read_line(ctrl->in, line_str) | Waits until a line has been read          |
| ctrl->await_finished(pid)                 | Waits until another process has completed |


## Thread Pools

Processes started in the PseudoNix system are always run on a single thread and
only when the `executeTaskQueue` is called. This is so that the processes
execute at known times within your application.

You may have a process that takes an exceptionally long time to load and may not
have a convenient way of yielding, for example, loading a large asset into
memory. You may want to load this asset in a background thread. 

One way of achieving this is to handle the background loading yourself. 

```c++
M.setFunction("loadAsset", [](e_type ctrl) -> task_type
{
    PSEUDONIX_PROC_START(ctrl);

    std::filesystem::path p(ARGS[1]);

    auto fut = std::async(std::launch::async, [&]()
    {
        // load p from the filesystem in a background
        // thread
    });

    while(fut.wait_for(std::chrono::seconds(0))==std::future_status::timeout )
    {
        co_await ctrl->await_yield();
    }

    auto asset = fut.get();
    // do what you need with asset
    co_return 0;
});
```

A much more convient way is to be able to run a portion of your coroutine on the
`MAIN` queue, and some of it on a different queue which is executed on a
background thread.

The following offloads the loading of the asset to the THREADPOOL queue, and
then returns to the MAIN queue when it is done.

```c++
M.setFunction("loadAsset", [](e_type ctrl) -> task_type
{
    PSEUDONIX_PROC_START(ctrl);

    std::filesystem::path p(ARGS[1]);

    // wait and resume on the THREADPOOL taskqueue
    HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield("THREADPOOL"), ctrl);

    auto asset = loadFromFile(p);

    // return to the main task queue
    HANDLE_AWAIT_INT_TERM(co_await ctrl->await_yield("MAIN"), ctrl);
            
    // do what you need with asset
    co_return 0;
});
```

This, by itself, doesn't do anything and will block forever. You need to
actually process the THREADPOOL queue. To do this, a special process, `bgrunner`
has been created for you to spawn a background thread that processes a queue.

```c++
PseudoNix::System M;
PseudoNix::enable_default_shell(M);
M.setFunction("launcher", PseudoNix::launcher_coro);

M.taskQueueCreate("THREADPOOL");

// Spawn 3 threads to process the THREADPOOL queue
M.spawnProcess({"bgrunner", "THREADPOOL"});
M.spawnProcess({"bgrunner", "THREADPOOL"});
M.spawnProcess({"bgrunner", "THREADPOOL"});

// Spawn the example queueHopper process
M.spawnProcess({"queueHopper", "THREADPOOL"});
M.spawnProcess({"queueHopper", "THREADPOOL"});
M.spawnProcess({"queueHopper", "THREADPOOL"});
M.spawnProcess({"queueHopper", "THREADPOOL"});
M.spawnProcess({"queueHopper", "THREADPOOL"});
```

You can even spawn this from the `shell` command by calling `bgrunner
THREADPOOL`.

Try it out using the terminal.

```bash
# Spawn 3 bgrunners to process the THREADPOOL queue in the background
spawn 3 bgrunner THREADPOOL

# execute the queueHopper example process 5 times
spawn 5 queueHopper THREADPOOL

```

## FileSystem

PseudoNix provides a virtual filesystem implementation. Files/Folders can exist
completely in memory, or be mounted from the host.

```c++

PseudoNix::System M;

// create a folder in the virtual file system
M.mkdir("/bin");

// create an empty file
M.mkfile("/bin/hello.sh");

// List all files/folders
for(auto u : M.list_dir(/mnt))
{
    COUT << std::format("{}\n", u.generic_string());
}
```

Some common filesystem utilities are also provided for the shell:

 * ls
 * cd
 * mount/umount
 * pwd
 * mkdir
 * touch
 * cp - single file only. No directories, no globbing
 * mv - single file only. No directories, no globbing

### Mounting Host Directories

You can mount a host directory inside the virtual file system using the following:

```c++

#include <PseudoNix/System.h>
#include <PseudoNix/Shell.h>
#include <PseudoNix/HostMount.h>
#include <PseudoNix/ArchiveMount.h> // requires libarchive

int main()
{
    PseudoNix::System M;

    PseudoNix::enable_default_shell(M); // gives you the default shell process
    PseudoNix::enable_host_mount(M);    // lets you mount host file systems    
    PseudoNix::enable_archive_mount(M); // lets you mount tar/tar.gz files

    // mount the user's home folderfolder
    M.mkdir("/host");
    sys.mount<PseudoNix::HostMount>("/host", "/home/user");

    // mount an uncompressed tar file
    M.mkdir("/tar");
    M.mount<PseudoNix::ArchiveMount>("/tar", "/path/to/archive.tar");
    
    // mount a compressed tar file
    M.mkdir("/tar.gz");
    M.mount<PseudoNix::ArchiveMount>("/tar", "/path/to/archive.tar.gz");

    // mount a tar file that is in embedded memory
    M.mkdir("/tar_embedded");
    M.mount<PseudoNix::ArchiveMount>("/tar_embedded", data_ptr, data_size);
}

```

### Accessing File Content

Now that you have either created virtual files or mounted host directories. You can 
can access the file data in a number of ways.

```c++
    // Simple append a string to an already created file
    M.mkfile("/path/to/file.txt");
    M.fs("/path/to/file.txt") << "Hello world";

    // write the data to a string
    std::string read;
    M.fs("/path/to/file.txt") >> read;

    // get an std::istream to read directly from
    // the data
    auto in = M.openRead("/path/to/file.txt");
    while(in.eof())
    {
        std::string word;
        in >> word;
    }
```

See the [Filesystem Unit Test](/test/unit-FileSystem2.cpp) for more usage.


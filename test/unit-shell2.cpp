#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#define PSEUDONIX_LOG_LEVEL_SYSTEM
#include <PseudoNix/System.h>
#include <PseudoNix/Shell.h>
#include <span>

using namespace PseudoNix;


Generator< std::optional<std::string> > BashTokenizerGen3(std::shared_ptr<System::stream_type> in)
{
    std::string _token;

    char c = 0;
    bool quoted = false;

    int bracket_count = 0;
    while(true)
    {
        auto res = in->get(&c);

        if(res == System::stream_type::Result::END_OF_STREAM)
        {
            if(!_token.empty())
                co_yield _token;
            co_yield ";";
            co_yield "done";
            co_return;
        }
        if(res == System::stream_type::Result::EMPTY)
        {
            co_yield std::nullopt;
            continue;
        }

        // will need to check whether we are in a quoted
        // string
        if(quoted)
        {
            if(c=='"')
            {
                quoted = !quoted;
            }
            else
            {
                _token.push_back(c);
            }
        }
        else
        {
            if(c == '"' && !quoted)
            {
                quoted = !quoted;
            }
            else if(c == ';')
            {
                if(!_token.empty())
                    co_yield _token;
                co_yield std::string("\n");
                _token.clear();
            }
            else if(c == '\n')
            {
                if(!_token.empty())
                    co_yield _token;
                co_yield std::string("\n");
                _token.clear();
            }
            else if( c == '(' )
            {
                _token.push_back(c);
                bracket_count++;
            }
            else if( c== ')')
            {
                _token.push_back(c);
                bracket_count--;
            }
            else if( c == ' ' && bracket_count == 0 )
            {
                if(!_token.empty())
                {
                    co_yield _token;
                    _token.clear();
                }
            }
            else
            {
                _token.push_back(c);
            }
        }
    }

    co_return;
}



Generator<std::optional<std::vector<std::string>>> bash_line_generator(std::shared_ptr<System::stream_type> s_in)
{
    auto gn = BashTokenizerGen3(s_in);

    std::vector<std::string> line_args;

    for(auto a : gn)
    {
        if(!a.has_value())
            co_yield std::nullopt;


        line_args.push_back(*a);
        if(line_args.back() == "\n")
        {
            line_args.pop_back();
            // Erase all the arguments after the first argument that starts with #
            line_args.erase(std::find_if(line_args.begin(), line_args.end(), [](auto const & s)
            {
                return s.size() && s.front() == '#' ;
            }), line_args.end());

            co_yield line_args;
            line_args.clear();
        }
    }
}

using WhatToDo3 = std::variant< std::string,                  // queue to hop onto
                                std::vector<System::pid_type>,  // PIDS-to wait on
                                int                             // 0 - do nothing
                               >;



inline
Generator<WhatToDo3> process_command(std::vector<std::string> args,
                                   System::ProcessControl * proc)
{
    // Erase all the arguments after the first argument that starts with #
    //args.erase(std::find_if(args.begin(), args.end(), [](auto const & s)
    //                        {
    //                            return s.size() && s.front() == '#' ;
    //                        }), args.end());

    if(!args.empty())
    {
        for(auto & v : args)
        {
            v = var_sub1(v, proc->env);
        }

        {
            // Check the PATH variable for
            // scripts that may exist there
            //

            auto & PATH = proc->env["PATH"];
            auto & SYSTEM = *proc->system;
            auto parts = PATH
                         | std::views::split(':')
                         | std::views::transform([](auto &&subrange) {
                               return std::string_view(&*subrange.begin(), static_cast<size_t>(std::ranges::distance(subrange)));
                           });
            for(auto subPath : parts)
            {
                auto bin_loc = System::path_type(subPath) / args[0];
                if(SYSTEM.exists(bin_loc))
                {
                    std::vector<std::string> newargs;

                    // Set all the argument variables $0, $1, $2...
                    // first
                    for(size_t i=0;i<args.size();i++)
                    {
                        newargs.push_back(std::format("{}={}", i, args[i]));
                    }
                    // add the sh shell

                    newargs.push_back(proc->args[0]);

                    // and the location of the script
                    newargs.push_back(bin_loc.generic_string());

                    args = newargs;
                    break;
                }
            }
        }

        bool run_in_background = false;
        if(args.back() == "&")
        {
            // NOTE: we should probably do a find for the &
            // and cut everything in front of it to be run
            // in the background
            run_in_background = true;
            args.pop_back();
        }

        //===============================================================
        // Process special shell functions
        //===============================================================
        if(args[0] == "yield")
        {
            // Yield to another Task queue
            if(args.size() == 1)
            {
                co_yield std::string(System::DEFAULT_QUEUE);
                proc->env["?"] = "0";
            }
            else if(args.size() >= 2 && proc->system->taskQueueExists(args[1]))
            {
                std::cout << "Hopping to queue: " << args[1] << std::endl;
                co_yield args[1];
                proc->env["?"] = "0";
            }
            else
            {
                *proc->out << std::format("Task queue, {}, does not exist. Staying on queue {}", args[1], proc->queue_name);
                proc->env["?"] = "1";
            }
            co_return;
        }
        //===============================================================

        if( run_in_background )
        {
#if 0
            auto STDIN = System::make_stream();
            STDIN->set_eof();
            auto pids = execute_pipes( args, ctrl.get(), STDIN, ctrl->out);
#else
            auto STDIN = System::make_stream();

            for(auto & a : args)
            {
                // pipe the data into stdin, and make sure each argument
                // is in quotes. We may need to tinker with this
                // to have properly escaped characters
                *STDIN << std::format("\"{}\" ", a);
            }
            *STDIN << std::format(";");
            STDIN->set_eof();
            auto pids = execute_pipes( {"sh", "--noprofile"}, proc, STDIN, proc->out);
#endif
            *proc->out << std::format("{}\n", pids[0]);
            proc->env["!"] = std::format("{}", pids[0]);
            co_return;
        }

        auto op_args = parse_operands(args);
        std::reverse(op_args.begin(), op_args.end());

        int ret_value = 0;
        while(op_args.size())
        {
            auto & cmd = op_args.back();
            auto _operator = cmd.front();

            if(_operator == "&&" && ret_value != 0)
            {
                op_args.pop_back();
                continue;
            }
            if(_operator == "||" && ret_value == 0)
            {
                op_args.pop_back();
                continue;
            }
            //======================================================================
            // Loop through all the arguments in the
            // cmd and see if any of them look like: $(cmdname arg1 arg2)
            //
            // If so, execute a new shell and pipe the "cmdname arg1 arg2" into
            // the input stream so that it can be executed
            for(auto it=cmd.begin(); it != cmd.end();)
            {
                if(it->size() >= 3 && it->substr(0,2) == "$(" && it->back() == ')')
                {
                    // we have a $(cmd arg1 arg2 arg3) situtation going on here
                    // so execute this as a new shell
                    auto STDIN = System::make_stream();
                    auto STDOUT = System::make_stream();
                    auto subProcess = execute_pipes( {"sh", "--noprofile"}, proc, STDIN, STDOUT);
                    *STDIN << it->substr(2, it->size()-3);
                    *STDIN << ';';
                    STDIN->set_eof(); // make sure to set the eof of the output stream otherwise
                        // sh will block waiting for bytes

                    co_yield subProcess;

                    std::string _out2;
                    *STDOUT >> _out2;
                    auto new_args = Tokenizer3::to_vector(_out2);
                    it = cmd.erase(it);
                    it = cmd.insert(it, new_args.begin(), new_args.end());
                }
                else
                {
                    ++it;
                }
            }

            //======================================================================

            auto subProcess = execute_pipes( std::vector(cmd.begin()+1, cmd.end()), proc, proc->in, proc->out);

            auto f_exit_code = proc->system->getProcessExitCode(subProcess.back());
            if(cmd.back() != "&")
            {
                co_yield subProcess;
                if(!f_exit_code)
                {
                    *proc->out << std::format("Command not found: [{}]\n", cmd[1] );
                    ret_value = 127;
                }
                else
                {
                    ret_value = *f_exit_code;
                }
            }
            proc->env["?"] = std::to_string(ret_value);

            op_args.pop_back();
        }
    }
    co_return;
}

inline
Generator<WhatToDo3> process_block(std::vector< std::vector<std::string> > script, System::ProcessControl * proc);

Generator<WhatToDo3> process_if(std::vector< std::vector<std::string> > script, System::ProcessControl * proc)
{
    // script must start with "if command"
    //
    //  if true
    //  then
    //     echo "true block"
    //   elif false
    //   then
    //     echo "elif-block"
    //   else
    //     echo "else-block"
    //   if

    assert(script.front().front() == "if");

    auto _find_next_block = [&script](auto first_it)
    {
        auto & S = *first_it;
        (void)S;
        assert(first_it->front() == "if" || first_it->front() == "elif" || first_it->front() == "else");
        ++first_it;
        size_t if_count = 1;
        while(first_it != script.end())
        {
            if( first_it->front() == "if") ++if_count;
            if( first_it->front() == "fi") --if_count;

            if((first_it->front() == "else" && if_count == 1) ||
                (first_it->front() == "elif" && if_count == 1) ||
                (first_it->front() == "fi" && if_count == 0) )
            {
                return first_it;
            }
            ++first_it;
        }
        return script.end();
    };


    System::exit_code_type exit_code = 1;
    auto first_it = script.begin() ;
    while(first_it != script.end())
    {
        auto second_it = _find_next_block(first_it);
        auto block_script = std::vector(first_it, second_it);
        first_it = second_it;

        if( exit_code != 0)
        {
            auto condition = std::vector(block_script[0].begin()+1, block_script[0].end());
            // regular command
            int skip_count = 1;
            if(block_script[0][0] == "else")
            {
                exit_code = 0;
            }
            else if(block_script[0][0] == "if" || block_script[0][0] == "elif")
            {
                auto preRet = proc->env["?"];
                for(auto c : process_command(condition, proc))
                {
                    auto ex = proc->system->getProcessExitCode( std::get<std::vector<System::pid_type>>(c).back() );
                    co_yield c;
                    exit_code = *ex;
                    break;
                }
                proc->env["?"] = preRet;
                skip_count = 2;
            }

            if(exit_code == 0)
            {
                auto block = std::vector(block_script.begin()+skip_count, block_script.end());
                for(auto &&cc : process_block(block, proc))
                {
                    co_yield cc;
                }
            }
        }

        assert(first_it != script.end());
        if(first_it->front() == "fi")
            break;
    }


}

Generator<WhatToDo3> process_while(std::vector< std::vector<std::string> > script, System::ProcessControl * proc)
{
    assert(script.front().front() == "while");
    assert(script[1].front() == "do");
    assert(script.back().front() == "done");

    System::exit_code_type exit_code = 1;

    while(true)
    {
        auto condition = std::vector(script[0].begin()+1, script[0].end());
        auto preRet = proc->env["?"];
        for(auto c : process_command(condition, proc))
        {
            auto ex = proc->system->getProcessExitCode( std::get<std::vector<System::pid_type>>(c).back() );
            co_yield c;
            exit_code = *ex;
            break;
        }
        proc->env["?"] = preRet;
        if(exit_code == 0)
        {
            auto block = std::vector(script.begin()+2, script.end()-1);
            for(auto &&cc : process_block(block, proc))
            {
                co_yield cc;
            }
        }
        else
        {
            break;
        }
    }
}


Generator<WhatToDo3> process_block(std::vector< std::vector<std::string> > script, System::ProcessControl * proc)
{
    for(size_t i=0; i < script.size(); i++)
    {
        if(script[i].front() == "if")
        {
            int if_count=1;
            auto j=i+1;
            while(true)
            {
                if(script[j].front() == "if") if_count++;
                if(script[j].front() == "fi") if_count--;
                if(if_count == 0)
                    break;
                ++j;
            }

            auto if_statement = std::vector( script.begin()+ssize_t(i), script.begin()+ssize_t(j)+1);
            for(auto &&c : process_if(if_statement, proc))
            {
                co_yield c;
            }
            i = j+1;
        }
        else if(script[i].front() == "while")
        {
            int if_count=1;
            auto j=i+1;
            while(true)
            {
                if(script[j].front() == "while") if_count++;
                if(script[j].front() == "done") if_count--;
                if(if_count == 0)
                    break;
                ++j;
            }

            auto while_statement = std::vector( script.begin()+ssize_t(i), script.begin()+ssize_t(j)+1);
            for(auto &&c : process_while(while_statement, proc))
            {
                co_yield c;
            }
            i = j+1;
        }
        else
        {
            for(auto &&c : process_command(script[i], proc))
            {
                co_yield c;
            }
        }
    }
}

inline System::task_type shell3_coro(System::e_type ctrl)
{
    PSEUDONIX_PROC_START(ctrl);

    auto _in  = ctrl->in;
    auto _out = ctrl->out;

    auto gn = bash_line_generator(ctrl->in);

    // Make sure this variable exists
    // and is exported otherwize
    // some of the shell commands wont work
    ENV["SHELL_PID"] = std::to_string(PID);
    EXPORTED["SHELL_PID"] = true;

    // the initial exit code
    ENV["?"] = "0";

    // Flag for when to exit the shell
    // this is used by the "exit" process
    // we might not need to use this
    auto & EXIT_SHELL = ENV["EXIT_SHELL"];
    EXIT_SHELL = {};

    std::string profile_script;
    bool load_etc_profile = true;
    {
        auto _args = ARGS;
        auto no_profile = std::find(_args.begin(), _args.end(), "--noprofile");
        if(_args.end() != no_profile)
        {
            // Copy the rc_text into the
            // the input stream so that
            // it will be executed first
            //script += shellEnv.rc_text;
            _args.erase(no_profile);
            load_etc_profile = false;
        }
        if(load_etc_profile && SYSTEM.exists("/etc/profile"))
        {
            profile_script += SYSTEM.file_to_string("/etc/profile");
        }
        if(_args.size() > 1)
        {
            if(SYSTEM.exists(_args[1]))
            {
                profile_script = SYSTEM.file_to_string(_args[1]);
                // add the exit command just in case
                // so that the shell command will return the last
                // exit code
                profile_script += "\nexit ${?};";
            }
            else
            {
                COUT << std::format("{}: {}: no such file or directory\n", ARGS[0], _args[1]);
            }
        }
        CIN << profile_script;
    }


    std::vector< std::vector<std::string> > script;
    int if_count=0;
    int while_count=0;
    System::exit_code_type ret_value = 0;
    auto a_it = gn.begin();
    while(a_it != gn.end())
    {
        auto a = *a_it;
        if(!a.has_value())
        {
            ++a_it;
            continue;
        }
        auto & line = *a;
        if(line.empty())
        {
            ++a_it;
            continue;
        }
        //std::cout << std::format("{}", join(line)) << std::endl;

        // Push the line into the script
        script.push_back(line);

        if(line.front() == "if") ++if_count;
        if(line.front() == "fi") --if_count;
        if(line.front() == "while") ++while_count;
        if(line.front() == "done")  --while_count;

        if(if_count == 0 && while_count == 0)
        {
            auto pp = process_block(script, ctrl.get());
            for(auto doWhat : pp)
            {
                if( std::holds_alternative<int>(doWhat) )
                {
                    if(std::get<int>(doWhat) == 0)
                    {
                        // do nothing
                    }
                }
                else if( std::holds_alternative<std::vector<System::pid_type>>(doWhat))
                {
                    auto & pids_to_wait_on = std::get<std::vector<System::pid_type>>(doWhat);
                    auto exit_code_p = SYSTEM.getProcessExitCode(pids_to_wait_on.back());
                    if(exit_code_p)
                    {
                        HANDLE_AWAIT_INT_TERM( co_await ctrl->await_finished(pids_to_wait_on), ctrl);
                        ctrl->out->_eof = false;
                        ctrl->env["?"] = std::to_string(*exit_code_p);
                    }
                    else
                    {
                        ctrl->env["?"] = "127";
                    }
                }
            }
            script.clear();
        }
        if(!EXIT_SHELL.empty())
        {
            //std::cout << "Shell exiting" << std::endl;
            break;
        }
        ++a_it;
    }
    if(std::errc() != std::from_chars(ENV["?"].data(), ENV["?"].data() + ENV["?"].size(), ret_value).ec)
    {
        co_return 0;
    }
    co_return std::move(ret_value);
}

#if 0
SCENARIO("Tokenizer Generator")
{
    std::string script = R"foo(
echo hello world
echo hello world2
if true; then
    echo hello if
elif false; then
    echo hello elif
else
    echo hello else
fi
echo finishied if
)foo";

    auto in = System::make_stream(script);
    in->set_eof();
    for(auto c : bash_line_generator(in))
    {
        if(!c->empty())
            std::cout << std::format("{}", join(*c)) << std::endl;;
    }
}


SCENARIO("Tokenizer Generator")
{
    std::string script = R"foo(
echo this is a test message

if false; then
    echo if
elif false; then
    echo elif
elif true; then
    echo elif2: ${CC}
else
    echo else
fi
)foo";

    System M;
    M.setFunction("sh", shell3_coro);

    auto pids = M.spawnPipelineProcess( {{"sh"}, {"to_std_cout"} });
    *M.getProcessControl(pids[0])->in << script;
    M.getProcessControl(pids[0])->in->set_eof();

    while(M.taskQueueExecute());

}
#endif


std::pair<std::string, System::exit_code_type> testS1(std::string script, bool from_file = false)
{
    System M;

    M.taskQueueCreate("PRE_MAIN");
    M.setFunction("sh", shell3_coro);

    M.touch("/script.sh");
    M.fs("/script.sh") << script;

    auto E1 = [&]()
    {
        if(from_file)
        {
            auto E = System::parseArguments({"sh", "/script.sh"});
            E.in   = System::make_stream();
            E.out  = System::make_stream();
            //E.in->set_eof();
            return E;
        }
        else
        {
            auto E = System::parseArguments({"sh"});
            // Here we're going to put our shell script code into the input
            // stream of the process function, similar to how linux works
            E.in  = System::make_stream(script);
            E.out = System::make_stream();
            E.in->set_eof();
            return E;
        }
    }();

    auto pid = M.runRawCommand(E1);
    REQUIRE(pid == 1);
    auto exit_code = M.getProcessExitCode(pid);

    while(M.taskQueueExecute("PRE_MAIN") + M.taskQueueExecute("MAIN"));

    auto str = E1.out->str();
    while(str.size() && str.back() == '\n')
        str.pop_back();
    return {str, *exit_code};
}

#if 0
SCENARIO("Test single line")
{
    auto [out, code] = testS1(R"foo(
echo hello world
)foo");

    REQUIRE(out == "hello world");
    REQUIRE(code == 0);

}


SCENARIO("Test two lines line")
{
    auto [out, code] = testS1(R"foo(
echo hello world
echo hello world
)foo");

    REQUIRE(out == "hello world\nhello world");
    REQUIRE(code == 0);
}

SCENARIO("Test comments")
{
    auto [out, code] = testS1(R"foo(
#echo hello world
echo hello # world
)foo");

    REQUIRE(out == "hello");
    REQUIRE(code == 0);
}

SCENARIO("Test exit codes")
{
    auto [out, code] = testS1(R"foo(
exit 0
)foo");

    REQUIRE(out == "");
    REQUIRE(code == 0);
}

SCENARIO("Test exit codes")
{
    auto [out, code] = testS1(R"foo(
exit 1
)foo");

    REQUIRE(out == "");
    REQUIRE(code == 1);
}


SCENARIO("Test exit codes")
{
    auto [out, code] = testS1(R"foo(
exit 156
echo after exit
)foo");

    REQUIRE(out == "");
    REQUIRE(code == 156);
}

SCENARIO("Test Single if: true condition")
{
    auto [out, code] = testS1(R"foo(
if true; then
    echo true
fi
)foo");

    REQUIRE(out == "true");
    REQUIRE(code == 0);
}

SCENARIO("Test Single if: false condition")
{
    auto [out, code] = testS1(R"foo(
if false; then
    echo true
fi
)foo");

    REQUIRE(out == "");
    REQUIRE(code == 0);
}

SCENARIO("Test Single if-else: false condition")
{
    auto [out, code] = testS1(R"foo(
echo before
if true; then
    echo true
else
    echo false
fi
echo after
)foo");

    REQUIRE(out == "before\ntrue\nafter");
    REQUIRE(code == 0);
}

SCENARIO("Test if-else: false condition")
{
    auto [out, code] = testS1(R"foo(
echo before
if false; then
    echo true
else
    echo false
fi
echo after
)foo");

    REQUIRE(out == "before\nfalse\nafter");
    REQUIRE(code == 0);
}


SCENARIO("Test if-elif-else: false condition")
{
    auto [out, code] = testS1(R"foo(
echo before
if true; then
    echo if
elif true; then
    echo elif
else
    echo else
fi
echo after
)foo");

    REQUIRE(out == "before\nif\nafter");
    REQUIRE(code == 0);
}

SCENARIO("Test if-elif-else: false condition")
{
    auto [out, code] = testS1(R"foo(
echo before
if false; then
    echo if
elif true; then
    echo elif
else
    echo else
fi
echo after
)foo");

    REQUIRE(out == "before\nelif\nafter");
    REQUIRE(code == 0);
}

SCENARIO("Test if-elif-else: false condition")
{
    auto [out, code] = testS1(R"foo(
echo before
if false; then
    echo if
elif false; then
    echo elif
else
    echo else
fi
echo after
)foo");

    REQUIRE(out == "before\nelse\nafter");
    REQUIRE(code == 0);
}


SCENARIO("Test nested if-statement")
{
    auto [out, code] = testS1(R"foo(
echo before
if true; then
    echo if
    if true; then
        echo if
    elif false; then
        echo elif
    else
        echo else
    fi
elif false; then
    echo elif
else
    echo else
fi
echo after
)foo");

    REQUIRE(out == "before\nif\nif\nafter");
    REQUIRE(code == 0);
}


SCENARIO("Test While-loop")
{
    auto [out, code] = testS1(R"foo(
echo before
while false; do
    echo false
done
echo after
)foo");

    REQUIRE(out == "before\nafter");
    REQUIRE(code == 0);
}

SCENARIO("Test While-loop")
{
    auto [out, code] = testS1(R"foo(
echo before
A=""
while test ${A} != AA; do
    A=${A}A
    B=""
    echo Outer
    while test ${B} != BB; do
        B=${B}B
        echo Inner
    done
done
echo after
)foo");

    REQUIRE(out == "before\nOuter\nInner\nInner\nOuter\nInner\nInner\nafter");
    REQUIRE(code == 0);
}

SCENARIO("Test While-loop")
{
    auto [out, code] = testS1(R"foo(
echo before
A=""
while test ${A} != AA; do
    A=${A}A
    B=""
    echo Outer
    while test ${B} != BB; do
        B=${B}B
        echo Inner
    done
done
echo after
)foo", true);

    REQUIRE(out == "before\nOuter\nInner\nInner\nOuter\nInner\nInner\nafter");
    REQUIRE(code == 0);
}

#endif

SCENARIO("Test Queue")
{
    auto [out, code] = testS1(R"foo(
echo ${QUEUE}
yield PRE_MAIN
echo ${QUEUE}
yield MAIN
echo ${QUEUE}
)foo", false);

    REQUIRE(out == "MAIN\nPRE_MAIN\nMAIN");
    REQUIRE(code == 0);
}



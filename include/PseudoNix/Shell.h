#ifndef PSEUDONIX_SHELL2_H
#define PSEUDONIX_SHELL2_H

#include <map>

#include "System.h"
#include "defer.h"

namespace PseudoNix
{

struct Tokenizer3
{
    std::string_view input;
    Tokenizer3(std::string_view v) : input(v)
    {
    }

    static std::vector<std::string> to_vector(std::string_view st)
    {
        std::vector<std::string> tokens;
        Tokenizer3 T2(st);
        auto t2=T2.next();
        while(!t2.empty())
        {
            tokens.push_back(t2);
            t2 = T2.next();
        }
        return tokens;
    }

    size_t pos=0;
    std::string next()
    {
        std::string current;
        bool quoted = false;

        while(pos < input.size())
        {
            char c = input[pos];
            auto sub = input.substr(pos, 2);

            if(!quoted)
            {
                if(sub[0] == '\\')
                {
                    current += sub[1];
                    pos += 2;
                }
                else if(sub == "$(" )
                {
                    size_t i=pos+2;
                    size_t b_count=1;
                    current += sub;
                    while(i<input.size())
                    {
                        if(input[i] == '(') b_count++;
                        if(input[i] == ')') b_count--;
                        current += input[i];
                        i++;
                        if(b_count==0)
                            break;
                    }
                    pos = i+1;
                    return current;
                }
                else if(sub == "&&" || sub == "||")
                {
                    if(!current.empty())
                        return current;
                    pos+=2;
                    return std::string(sub);
                }
                else if(c==')' || c == '|' || c=='(')
                {
                    if(!current.empty())
                        return current;
                    pos+=1;
                    return std::string(1,c);
                }
                else if( c == '"')
                {
                    quoted = !quoted;
                    pos += 1;
                }
                else
                {
                    if( !std::isspace(c) )
                    {
                        current += c;
                        pos += 1;
                    }
                    else
                    {
                        if(!current.empty())
                            return current;
                        else
                            pos += 1;
                    }
                }
            }
            else
            {
                if( c == '"')
                    quoted = !quoted;
                else
                {
                    current += c;
                }
                pos += 1;
            }

        }
        if(!current.empty())
        {
            auto s = std::move(current);
            return s;
        }
        return {};
    }
};



struct ShellEnv;
inline std::map<uint32_t, ShellEnv> _shells;

struct ShellEnv
{
    std::string                        rc_text;
    std::map<std::string, bool>        exportedVar;
    std::map<std::string, std::string> env;
    bool                               exitShell = false;
    System::pid_type                   shellPID = 0;
    bool                               isSigTerm = false;

    // child PIDs
    struct Internal_
    {
        std::vector<System::pid_type>   pids;
    };
    std::shared_ptr<Internal_> internal = std::make_shared<Internal_>();


    static void setFuncs(System * L)
    {
        #define _GET_SHELL \
                System::pid_type shell_pid = static_cast<System::pid_type>(std::stoul(ex->env["SHELL_PID"]));\
                    auto *shell = &_shells[shell_pid];\
                    if(!shell)\
                    co_return 0;\

        auto & f = L->m_funcs;

        f["exit"] = [](System::e_type ex) -> System::task_type
            {
                _GET_SHELL

                shell->exitShell = true;
                co_return 0;
        };
        f[""] = [](System::e_type ex) -> System::task_type
        {
            _GET_SHELL
            (void)ex;
            // This function will be called, if we set environment variables
            // but didn't call an actual function, eg:
            //     VAR=value VAR2=value2
            for(auto & [var,val] : ex->env)
            {
                shell->env[var] = val;
            }
            co_return 0;
        };
        f["export"] = [](System::e_type ex) -> System::task_type
        {
            _GET_SHELL

            // used to export variables
            for(size_t i=1;i<ex->args.size();i++)
            {
                auto [var,val] = System::splitVar(ex->args[i]);
                if(!var.empty() && !val.empty())
                {
                    // if the arg looked like: VAR=VAL
                    // then set the variable as well as
                    // export it
                    shell->exportedVar[std::string(var)] = true;
                    shell->env[std::string(var)] = val;
                }
                else
                {
                    // just export the variable
                    shell->exportedVar[std::string(ex->args[i])] = true;
                }
            }
            co_return 0;
        };

        f["exported"] = [](System::e_type ex) -> System::task_type
        {
            _GET_SHELL
            // used to export variables
            (void)ex;
            for(auto & x : shell->exportedVar)
            {
                *ex->out << x.first << '\n';
            }
            co_return 0;
        };
    }
};


inline std::vector<System::pid_type> execute_pipes(std::vector<std::string> tokens,
                             System::ProcessControl * proc,
                             ShellEnv * exported_environment,
                             std::shared_ptr<System::stream_type> in={},
                             std::shared_ptr<System::stream_type> out={})
{
    auto first = tokens.begin();
    auto last = std::find(first, tokens.end(), "|");

    std::vector<std::vector<std::string>> list_of_args;

    while(last != tokens.end())
    {
        list_of_args.push_back(std::vector(first, last));
        first = last+1;
        last = std::find(first, tokens.end(), "|");
    }
    list_of_args.push_back(std::vector(first, last));


    auto E = System::genPipeline(list_of_args);
    E.front().in = in;
    E.back().out = out;

    // Copy all the exported varaibles to
    // each of the processes we're going to be running
    for(auto & [name, exp] : exported_environment->exportedVar)
    {
        if(exported_environment->env.count(name))
        {
            for(auto & e : E)
                e.env[name] = exported_environment->env[name];
        }
    }

    for(auto & e : E)
    {
        // Copy the SHELL_PID value so that
        // calling functions can know which shell its running under
        e.env["SHELL_PID"] = std::to_string(exported_environment->shellPID);

        // we set variables, but didnt call a function, eg: CC=gcc CXX=g++
        //
        // We can use this to set environment variables for the shell
        //
        if(e.args.empty())
        {
            e.args.push_back("");
        }
    }

    auto pids = proc->executeSubPipeline(E);

    return pids;
}


inline std::vector< std::vector<std::string> > parse_operands(std::vector<std::string> tokens)
{
    std::vector< std::vector<std::string> > args(1);
    args.back().push_back(")(");
    for(auto & a : tokens)
    {
        if( a == "&&" || a == "||" )
        {
            args.push_back({});
        }
        args.back().push_back(a);
    }
    return args;
}



/**
 * @brief var_sub
 * @param str
 * @param env
 * @return
 *
 * Given a string that contains ${VARNAME} or $VARNAME, and the env map, substitue
 * the appropriate variables and return a new string.
 */
inline std::string var_sub1(std::string_view str, std::map<std::string,std::string> const & env)
{
    (void)env;
    std::string outstr;

    for(size_t i=0;i<str.size();i++)
    {
        auto sub = str.substr(i,2);

        if(sub == "$?" || sub == "${" || (sub[0]=='$' && std::isalpha(sub[1])) )
        {
            std::string var_name;
            for(i=i+1; i<str.size(); i++)
            {
                if(str[i] == '}' || std::isspace(str[i]))
                {
                    break;
                }
                var_name += str[i];
            }
            auto it = env.find((var_name.size() && var_name.front() == '{') ? var_name.substr(1) : var_name);
            if(it != env.end())
                outstr += it->second;
        }
        else
        {
            outstr += str[i];
        }
    }
    return outstr;
}


inline System::task_type shell_coro(System::e_type control, ShellEnv shellEnv1)
{
    std::string _current;

    int ret_value = 0;

    auto & exev = *control;

    auto shellPID = control->get_pid();
    auto & shellEnv = _shells[shellPID];

    shellEnv = shellEnv1;
    ShellEnv::setFuncs(control->mini);

    if(control->args.end() == std::find(control->args.begin(), control->args.end(), "--noprofile"))
    {
        // Copy the rc_text into the
        // the input stream so that
        // it will be executed first
        *exev.in << shellEnv.rc_text;
    }

    // Copy the additional environment variables
    // into our workinv variables
    for(auto & [var, val] : exev.env)
    {
        shellEnv.env[var] = val;
    }
    shellEnv.shellPID = control->get_pid();
    assert(shellEnv.shellPID != 0xFFFFFFFF);

    std::string shell_name = control->args[0];


    #define SHOULD_QUIT exev.in->eof()

    std::vector<System::pid_type> subProcess;
#define USE_AWAITERS
    // control->setSignalHandler([](int s)
    // {
    //     std::cerr << std::format("Custom signal recieved: {}", s) << std::endl;;
    // });
    while(!shellEnv.exitShell)
    {

#if defined USE_AWAITERS
        HANDLE_AWAIT_TERM(co_await control->await_has_data(control->in), control);

        char c;

        auto r = control->in->get(&c);

        if(r == System::stream_type::Result::END_OF_STREAM)
        {
            shellEnv.exitShell = true;
            break;
        }
        else if(r == System::stream_type::Result::EMPTY)
        {
            // unlikely this would happen because of
            // the await
            break;
        }
#else
        //if(SHOULD_QUIT || shellEnv.exitShell) co_return 0;
        while(!shellEnv.exitShell)
        {
            if(SHOULD_QUIT)
                co_return 0;

            if(exev.eof())
            {
                //std::cerr << "Shell: EOF on input" << std::endl;
            }
            if(exev.has_data())
                break;

            SUSPEND_POINT(control);
        }
        auto c = control->in->get();
#endif

        _current += c;
        if(c != ';' && c != '\n')
        {
            continue;
        }
        _current.pop_back();

        if(_current.empty())
        {
            continue;
        }

        auto args = Tokenizer3::to_vector(var_sub1(_current, shellEnv.env));
        _current.clear();

        bool run_in_background = false;

        if(args.back() == "&")
        {
            run_in_background = true;
            args.erase(args.end()-1);
        }
        else if(args.back().back() == '&')
        {
            args.back().pop_back();
            run_in_background = true;
        }

        if( run_in_background )
        {
            auto stdin = System::make_stream();

            for(auto & a : args)
            {
                // pipe the data into stdin, and make sure each argument
                // is in quotes. We may need to tinker with this
                // to have properly escaped characters
                *stdin << std::format("\"{}\" ", a);
            }
            *stdin << std::format(";");
            stdin->close();
            stdin->set_eof();
            auto pids = execute_pipes( {shell_name, "--noprofile"}, &exev, &shellEnv, stdin, exev.out);

            *exev.out << std::to_string(pids[0]) << "\n";
            continue;
        }

        auto op_args = parse_operands(args);

        std::reverse(op_args.begin(), op_args.end());

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
                    auto stdin  = System::make_stream();
                    auto stdout = System::make_stream();
                    subProcess = execute_pipes( {shell_name, "--noprofile"}, &exev, &shellEnv, stdin, stdout);
                    *stdin << it->substr(2, it->size()-3);
                    *stdin << ';';
                    stdin->set_eof();
                    stdin->close(); // make sure to close the output stream otherwise
                                    // sh will block waiting for bytes

                    HANDLE_AWAIT_TERM(co_await control->await_finished(subProcess), control)

                    std::string out;
                    *stdout >> out;
                    auto new_args = Tokenizer3::to_vector(out);
                    it = cmd.erase(it);
                    it = cmd.insert(it, new_args.begin(), new_args.end());
                }
                else
                {
                    ++it;
                }
            }

            //======================================================================

            auto stdout = System::make_stream();
            subProcess = execute_pipes( std::vector(cmd.begin()+1, cmd.end()), &exev, &shellEnv, exev.in, exev.out);
            auto f_exit_code = exev.mini->processExitCode(subProcess.back());

            HANDLE_AWAIT_TERM(co_await control->await_finished(subProcess), control)

            if(!f_exit_code)
            {
                *exev.out << std::format("Command not found: [{}]\n", cmd[1] );
                ret_value = 127;
            }
            else
            {
                ret_value = *f_exit_code;
            }

            shellEnv.env["?"] = std::to_string(ret_value);

            op_args.pop_back();
        }
    }

    *control->out << std::format("exit");
    co_return ret_value;
}


}

#endif

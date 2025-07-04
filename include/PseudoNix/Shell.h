#ifndef PSEUDONIX_SHELL2_H
#define PSEUDONIX_SHELL2_H

#include <map>

#include "System.h"
#include "defer.h"
#include <ranges>
#include <variant>

namespace PseudoNix
{

struct Tokenizer4
{
    std::string_view input;
    Tokenizer4(std::string_view v) : input(v)
    {
    }

    static std::vector<std::string> to_vector(std::string_view st)
    {
        std::vector<std::string> tokens;
        Tokenizer4 T2(st);
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
                else if(c==')' || c == '|' || c=='(' || c== '#')
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

using Tokenizer = Tokenizer4;

enum class StreamError : int8_t {
    EMPTY, //
    END_OF_STREAM
};

template<typename T, typename E>
struct Expected : private std::variant<T, E>
{
    using value_type   = T;
    using error_type   = E;
    using variant_type = std::variant<T, E>;

    Expected()
        : variant_type()
    {}

    Expected(value_type const &t)
        : variant_type(t) {};

    Expected(value_type &&t)
        : variant_type(std::move(t)) {};

    Expected(error_type const &t)
        : variant_type(t) {};

    Expected(error_type &&t)
        : variant_type(std::move(t)) {};

    bool operator==(error_type const &e) const
    {
        if (std::holds_alternative<error_type>(*this))
        {
            return std::get<error_type>(*this) == e;
        }
        return false;
    }
    bool operator==(value_type const &e) const
    {
        if (std::holds_alternative<value_type>(*this))
        {
            return std::get<value_type>(*this) == e;
        }
        return false;
    }

    value_type &value()
    {
        return std::get<value_type>(*this);
    }
    value_type const &value() const
    {
        return std::get<value_type const>(*this);
    }
    error_type &error()
    {
        return std::get<value_type>(*this);
    }
    error_type const &error() const
    {
        return std::get<value_type const>(*this);
    }
};

inline Generator<Expected<char, StreamError>> streamGenerator(std::shared_ptr<System::stream_type> in)
{
    static_assert(sizeof(Expected<char, StreamError>) == 2);

    char c = 0;
    while (true)
    {
        auto res = in->get(&c);
        switch (res)
        {
        case ReaderWriterStream_t<char>::Result::SUCCESS:
            co_yield c;
            break;
        case ReaderWriterStream_t<char>::Result::EMPTY:
            co_yield StreamError::EMPTY;
            break;
        case ReaderWriterStream_t<char>::Result::END_OF_STREAM:
            co_yield StreamError::END_OF_STREAM;
            co_return;
            break;
        }
    }
}

inline Generator<Expected<char, StreamError>> streamGenerator(std::string &&in)
{
    static_assert(sizeof(Expected<char, StreamError>) == 2);

    for (auto c : in)
    {
        co_yield c;
    }
    //co_yield StreamError::END_OF_STREAM;
}

inline Generator<std::optional<std::string>> bashTokenGenerator(Generator<Expected<char, StreamError>> in)
{
    std::string _token;

    //char c      = 0;
    bool quoted = false;

    int bracket_count  = 0;
    bool comment_found = false;
    for (auto it : in)
    {
        if (it == StreamError::END_OF_STREAM)
        {
            break;
        }
        if (it == StreamError::EMPTY)
        {
            co_yield std::nullopt;
            continue;
        }
        char c = it.value();
        // will need to check whether we are in a quoted
        // string
        if (quoted)
        {
            if (c == '"')
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
            if (bracket_count == 0 && (c == ';' || c == '\n'))
            {
                if (!_token.empty())
                    co_yield _token;
                co_yield std::string("\n");
                _token.clear();
                comment_found = false;
            }
            else
            {
                if (comment_found)
                {
                    // dont add anything
                }
                else
                {
                    if (c == '"' && !quoted)
                    {
                        quoted = !quoted;
                    }
                    else if (c == '#')
                    {
                        comment_found = true;
                    }
                    else if (c == '(')
                    {
                        _token.push_back(c);
                        bracket_count++;
                    }
                    else if (c == ')')
                    {
                        _token.push_back(c);
                        bracket_count--;
                    }
                    else if (c == ' ' && bracket_count == 0)
                    {
                        if (!_token.empty())
                        {
                            co_yield _token;
                            _token.clear();
                        }
                    }
                    else
                    {
                        if (!comment_found)
                            _token.push_back(c);
                    }
                }
            }
        }
    }

    if (!_token.empty())
        co_yield _token;
    co_yield ";";
    co_yield "done";
    co_return;
}

inline
Generator< std::optional<std::string> > bashTokenGenerator(std::shared_ptr<System::stream_type> in)
{
#if 1
    return bashTokenGenerator(streamGenerator(in));
#else
    std::string _token;

    char c = 0;
    bool quoted = false;

    int bracket_count = 0;
    bool comment_found = false;
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
            if(c == ';' || c == '\n')
            {
                if(!_token.empty())
                    co_yield _token;
                co_yield std::string("\n");
                _token.clear();
                comment_found=false;
            }
            else
            {
                if(comment_found)
                {
                    // dont add anything
                }
                else
                {
                    if(c == '"' && !quoted)
                    {
                        quoted = !quoted;
                    }
                    else if(c=='#')
                    {
                        comment_found = true;
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
                        if(!comment_found)
                            _token.push_back(c);
                    }
                }
            }
        }
    }

    co_return;
#endif
}

template<typename Gen_t>
auto catGenerator(Gen_t &&A, Gen_t &&B) -> Generator<typename Gen_t::value_type>
{
    for (auto i : A)
    {
        co_yield i;
    }
    for (auto i : B)
    {
        co_yield i;
    }
    co_return;
};

inline Generator<std::vector<std::string>> bashLineGenerator(Generator<Expected<char, StreamError>> char_gen)
{
    auto gn = bashTokenGenerator(std::move(char_gen));

    std::vector<std::string> line_args;

    for(auto a : gn)
    {
        if (!a.has_value())
        {
            co_yield {};
            continue;
        }

        line_args.push_back(*a);
        if(line_args.back() == "\n")
        {
            line_args.pop_back();
            // Erase all the arguments after the first argument that starts with #
            //line_args.erase(std::find_if(line_args.begin(), line_args.end(), [](auto const & s)
            //                             {
            //                                 return s.size() && s.front() == '#' ;
            //                             }), line_args.end());

            if(!line_args.empty())
                co_yield line_args;
            line_args.clear();
        }
    }
}


using WhatToDo3 = std::variant< std::string,                  // queue to hop onto
                               std::vector<System::pid_type>,  // PIDS-to wait on
                               int                             // 0 - do nothing, 1 - break loop, 2 - continue loop
                               >;

inline std::vector<System::pid_type> execute_pipes(std::vector<std::string> tokens,
                                                   System::ProcessControl * proc,
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
    if(E.size())
    {
        E.front().in = in;
        E.back().out = out;
    }

    for(auto & e : E)
    {
        if(e.env.size() > 0 && e.args.size() == 0)
        {
            e.args.push_back("");
        }
        e.queue = proc->queue_name;
        //e.user_id = user_id;
    }

    auto me = proc->get_pid();
    auto pids = proc->system->runPipeline(E, me, true);
    auto my_cwd = proc->system->getProcessControl(me)->cwd;
    for(auto p : pids)
    {
        if(p != invalid_pid)
            proc->system->getProcessControl(p)->chdir(my_cwd);
    }

    // Loop through all the PID's and execute them
    // one at at time. If the process is quick and does not
    // suspend, then we dont have to wait until the execution
    // of the queue for it to start
    //
    //for(auto p  : pids)
    //{
    //    if(p != invalid_pid)
    //        proc->system->resume(p);
    //}

    // for each of the PID's do an initial resume
    // just in case the process can finish quickly
    //pids.erase(std::remove_if(pids.begin(),
    //                          pids.end(),
    //                          [sys = proc->system](auto p)
    //                          {
    //                              if (p == invalid_pid)
    //                                  return true;
    //                              sys->resume(p);
    //                              (void) p;
    //                              (void) sys;
    //                              return sys->processGetState(p) == System::Process::EXITED;
    //                          }),
    //           pids.end());
    //
    //
    return pids;
}

/**
 * @brief parse_operands
 * @param tokens
 * @return
 *
 * Given a single vector of string arguments separated by && or ||
 * Return a vector of vector<strings> where each internal vector
 * is prepended with either &&, || or )(
 */
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

        if(sub == "$?" || sub == "$!" || sub == "${" || (sub[0]=='$' && std::isalnum(sub[1])) )
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



inline
Generator<WhatToDo3> process_command(std::vector<std::string> args,
                                     System::ProcessControl * proc)
{
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
                co_yield proc->queue_name;
                proc->env["?"] = "0";
            }
            else if(args.size() >= 2 && proc->system->taskQueueExists(args[1]))
            {
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

        // We need to check if any of the arguments contain
        // sub commands: eg $(cmd arg1 ag2...)
        for (auto &A : args)
        {
            while (true)
            {
                auto i = A.find_first_of("$(");
                if (i == std::string::npos)
                    break;

                size_t k = 1;
                for (size_t j = i + 2; j < A.size(); j++)
                {
                    if (A[j] == '(')
                        k++;
                    if (A[j] == ')')
                        k--;
                    if (k == 0)
                    {
                        auto subCmd = A.substr(i + 2, j - i - 2);
                        //std::cout << subCmd << std::endl;
                        auto STDIN  = System::make_stream();
                        STDIN->eof();
                        auto STDOUT = System::make_stream();
                        auto pids   = execute_pipes({"sh", "-c", subCmd}, proc, STDIN, STDOUT);
                        co_yield pids;

                        auto output = STDOUT->str();
                        while (output.back() == '\n')
                            output.pop_back();
                        A = A.substr(0, i) + output + A.substr(j + 1);
                        //std::cout << A << std::endl;
                        break;
                    }
                }
                if (k != 0)
                {
                    co_return;
                }
            }
        }

        if (run_in_background)
        {
            bool is_chained = std::count_if(args.begin(),
                                            args.end(),
                                            [](auto &&str) {
                                                return str == "|" || str == "&&" || str == "||";
                                            })
                              > 0;

            auto STDIN = System::make_stream();

            if (!is_chained)
            {
                // not a chained cmd: eg  cmd1 || cmd2 && cmd3
                auto pids = execute_pipes(args, proc, STDIN, proc->out);
                *proc->out << std::format("{}\n", pids[0]);
                proc->env["!"] = std::format("{}", pids[0]);

                for (auto p : pids)
                {
                    proc->system->resume(p);
                }
            }
            else
            {
                for (auto &a : args)
                {
                    // pipe the data into stdin, and make sure each argument
                    // is in quotes. We may need to tinker with this
                    // to have properly escaped characters
                    *STDIN << std::format("\"{}\" ", a);
                }
                *STDIN << std::format(";");
                STDIN->set_eof();
                auto pids = execute_pipes({"sh", "--noprofile"}, proc, STDIN, proc->out);

                *proc->out << std::format("{}\n", pids[0]);
                proc->env["!"] = std::format("{}", pids[0]);

                for (auto p : pids)
                {
                    proc->system->resume(p);
                }
            }
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
                    auto new_args = Tokenizer4::to_vector(_out2);
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

inline
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

        // loop through all the blocks: ie the if, elif and else blocks
        // and execute them only if the last exit code was zero.
        // it is initially set as zero so the first if-statement
        // will always be run
        if( exit_code != 0)
        {
            auto condition = std::vector(block_script[0].begin()+1, block_script[0].end());
            if(condition[0] == "[[" && condition.back() == "]]")
            {
                condition[0] = "test";
                condition.pop_back();
            }
            // regular command
            int skip_count = 1;
            if(block_script[0][0] == "else")
            {
                exit_code = 0;
            }
            else if(block_script[0][0] == "if" || block_script[0][0] == "elif")
            {
                // if we find an if or elif statement
                // then we have to process
                auto preRet = proc->env["?"];
                for(auto c : process_command(condition, proc))
                {
                    auto ex = proc->system->getProcessExitCode( std::get<std::vector<System::pid_type>>(c).back() );
                    co_yield c;
                    // set the exit code of the condition statement
                    // if it is non-zero, the rest of the blocks will be
                    // ignored
                    exit_code = *ex;
                    break;
                }
                proc->env["?"] = preRet;
                skip_count = 2;
            }

            if(exit_code == 0)
            {
                // if the previous command (condition) exit code is 0, then we can
                // execute the block;
                auto block = std::vector(block_script.begin()+skip_count, block_script.end());
                // std::cout << std::format("IF BLOCK START<{}>", join(block[0])) << std::endl;
                for(auto &&cc : process_block(block, proc))
                {
                    if(std::holds_alternative<int>(cc) )
                    {
                        if(std::get<int>(cc) == 1)
                        {
                            //std::cout << "Break found in IF" << std::endl;
                            co_yield cc;
                            break; // break main loop
                        }
                        if(std::get<int>(cc) == 2)
                        {
                            //std::cout << "Continue found in IF" << std::endl;
                            co_yield cc;
                            break;
                        }
                    }
                    else
                    {
                        //std::cout << "co_yield" << std::endl;
                        co_yield cc;
                    }
                }
                //std::cout << std::format("IF BLOCK END <{}>", join(block[0])) << std::endl;
            }
        }

        assert(first_it != script.end());
        if(first_it->front() == "fi")
            break;
    }
    //std::cout << std::format("<END IF>") << std::endl;;

}

inline
Generator<WhatToDo3> process_while(std::vector< std::vector<std::string> > script, System::ProcessControl * proc)
{
    assert(script.front().front() == "while");
    assert(script[1].front() == "do");
    assert(script.back().front() == "done");

    System::exit_code_type exit_code = 1;

    bool _break = false;
    int _maxCount=0;
    while(!_break && _maxCount++ < 6)
    {
        auto condition = std::vector(script[0].begin()+1, script[0].end());
        if(condition[0] == "[[" && condition.back() == "]]")
        {
            condition[0] = "test";
            condition.pop_back();
        }
        auto preRet = proc->env["?"];
        //std::cout << std::format("Checking Condition: {}", join(condition)) << std::endl;
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
            //std::cout << "<WHILE BLOCK START>" << std::endl;
            for(auto &&cc : process_block(block, proc))
            {
                if(std::holds_alternative<int>(cc) )
                {
                    if(std::get<int>(cc) == 1)
                    {
                        //std::cout << "Break found in loop" << std::endl;
                        _break = true; // break main loop
                        break;
                    }
                    else if(std::get<int>(cc) == 2)
                    {
                        //std::cout << "Continue found in loop" << std::endl;
                        co_yield cc;
                        exit_code = 1;
                        break;
                    }
                }
                else
                {
                    //std::cout << "co_yield" << std::endl;
                    co_yield cc;
                }
            }
            //std::cout << "<WHILE BLOCK END>\n\n" << std::endl;
        }
        else
        {
            break;
        }
    }
    //std::cout << "While loop exited" << std::endl;
}

inline
Generator<WhatToDo3> process_for(std::vector< std::vector<std::string> > script, System::ProcessControl * proc)
{
    assert(script.front().front() == "for");
    assert(script[1].front() == "do");
    assert(script.back().front() == "done");

    assert(script.front()[2] == "in");
    assert(script.size() >= 4);

    // for VARNAME in LIST OF ITEMS
    auto VARNAME = script.front()[1];

    auto preRet = proc->env[VARNAME];
    bool _break = false;
    for(auto && item : script.front() | std::views::drop(3))
    {
        if(_break)
            break;
        proc->env[VARNAME] = item;
        auto block = std::vector(script.begin()+2, script.end()-1);
        for(auto &&cc : process_block(block, proc))
        {
            if(std::holds_alternative<int>(cc) )
            {
                if(std::get<int>(cc) == 1)
                {
                    co_yield cc;
                    _break = true; // break main loop
                }
            }
            else
            {
                co_yield cc;
            }
        }
    }
}

inline
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

            auto if_statement = std::vector(script.begin() + static_cast<std::ptrdiff_t>(i),
                                            script.begin() + static_cast<std::ptrdiff_t>(j) + 1);
            for(auto &&c : process_if(if_statement, proc))
            {
                co_yield c;
            }
            i = j;
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

            auto while_statement = std::vector(script.begin() + static_cast<std::ptrdiff_t>(i),
                                               script.begin() + static_cast<std::ptrdiff_t>(j) + 1);
            for(auto &&c : process_while(while_statement, proc))
            {
                co_yield c;
            }
            i = j;
        }
        else if(script[i].front() == "for")
        {
            int if_count=1;
            auto j=i+1;
            while(true)
            {
                if(script[j].front() == "for") if_count++;
                if(script[j].front() == "done") if_count--;
                if(if_count == 0)
                    break;
                ++j;
            }

            auto for_statement = std::vector(script.begin() + static_cast<std::ptrdiff_t>(i),
                                             script.begin() + static_cast<std::ptrdiff_t>(j) + 1);
            for(auto &&c : process_for(for_statement, proc))
            {
                co_yield c;
            }
            i = j+1;
        }
        else if(script[i].front() == "break")
        {
            co_yield 1;
            co_return;
        }
        else if(script[i].front() == "continue")
        {
            co_yield 2;
            co_return;
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

inline bool has_flag(std::vector<std::string> &_args, std::string flag)
{
    auto no_profile = std::find(_args.begin(), _args.end(), flag);
    if (no_profile == _args.end())
    {
        return false;
    }
    _args.erase(no_profile, no_profile + 1);
    return true;
}

inline System::task_type shell_coro(System::e_type ctrl)
{
    PN_PROC_START(ctrl);

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

    std::chrono::duration max_processing_time = std::chrono::milliseconds(1);

    std::string profile_script;
    bool load_etc_profile = true;

    do
    {
        auto _args = ARGS;

        std::string command;
        if (System::_has_arg(_args, "-c", command))
        {
            profile_script  = std::move(command);
            profile_script += "\nexit ${?};";
            break;
        }

        load_etc_profile = !System::_has_flag(_args, "--noprofile");

        if (_args.size() > 1)
        {
            // Loading a script from a file
            auto script_to_load_path = System::path_type(_args[1]);
            PN_HANDLE_PATH(CWD, script_to_load_path);

            PN_PROC_CHECK(!SYSTEM.exists(script_to_load_path),
                          "{}: {}: no such file or directory",
                          _args[0],
                          script_to_load_path.generic_string());

            SYSTEM.fs(script_to_load_path) >> profile_script;
            profile_script += "\nexit ${?};";
        }
        else if (load_etc_profile && SYSTEM.exists("/etc/profile"))
        {
            SYSTEM.fs("/etc/profile") >> profile_script;
        }

    } while (false);

    std::vector<std::vector<std::string>> script;
    int if_count=0;
    int while_count=0;
    System::exit_code_type ret_value = 0;

    auto gn_prof = bashLineGenerator(streamGenerator(std::move(profile_script)));
    auto gn_in   = bashLineGenerator(streamGenerator(ctrl->in));

    // Create a single line generator from the two input generators: the profile script
    // and the input stream
    auto gn   = catGenerator(std::move(gn_prof), std::move(gn_in));
    auto a_it = gn.begin();

    while (a_it != gn.end())
    {
        auto line = *a_it;
        if(line.empty())
        {
            PN_HANDLE_AWAIT_TERM( co_await ctrl->await_has_data(ctrl->in), ctrl);
            ++a_it;
            continue;
        }

        // Push the line into the script
        script.push_back(line);

        if(line.front() == "if") ++if_count;
        if(line.front() == "fi") --if_count;
        if(line.front() == "for") ++while_count;
        if(line.front() == "while") ++while_count;
        if(line.front() == "done")  --while_count;

        if (if_count == 0 && while_count == 0)
        {
            auto pp = process_block(script, ctrl.get());

            for (auto doWhat : pp)
            {
                if( std::holds_alternative<int>(doWhat) )
                {
                    // do nothing
                }
                else if( std::holds_alternative<std::string>(doWhat))
                {
                    // if it retuns a string, it means we have to
                    // yield and pop onto another queue
                    PN_HANDLE_AWAIT_TERM( co_await ctrl->await_yield(std::get<std::string>(doWhat)), ctrl);
                }
                else if( std::holds_alternative<std::vector<System::pid_type>>(doWhat))
                {
                    // if it returns a vector of pids, we need to wait on them
                    auto & pids_to_wait_on = std::get<std::vector<System::pid_type>>(doWhat);

                    {
                        // Each of the PID's are returned in a suspended state
                        // so do one run of resuming all of them just in case
                        // they are quick to exit
                        pids_to_wait_on.erase(std::remove_if(pids_to_wait_on.begin(),
                                                             pids_to_wait_on.end(),
                                                             [sys = &SYSTEM](auto p)
                                                             {
                                                                 if (p == invalid_pid)
                                                                     return true;
                                                                 sys->resume(p);
                                                                 return sys->processGetState(p)
                                                                        == System::Process::EXITED;
                                                             }),
                                              pids_to_wait_on.end());
                    }

                    if (!pids_to_wait_on.empty())
                    {
                        auto exit_code_p = SYSTEM.getProcessExitCode(pids_to_wait_on.back());
                        if (exit_code_p)
                        {
                            // we need to check if any of the PID's have been paused
                            // if they have, then we need to move it into the background
                            PN_WAIT(pids_to_wait_on);

                            ctrl->out->_eof = false;
                            ctrl->env["?"]  = std::to_string(*exit_code_p);
                        }
                        else
                        {
                            ctrl->env["?"] = "127";
                        }
                    }
                }
            }
            script.clear();

            PN_YIELD_IF(max_processing_time);
        }
        if(!EXIT_SHELL.empty())
        {
            break;
        }
        ++a_it;
    }

    if(!to_number(ENV["?"], ret_value))
    {
        co_return 0;
    }
    co_return std::move(ret_value);
}

inline void enable_default_shell(System & sys)
{
    sys.setFunction("sh", "Default Shell", PseudoNix::shell_coro);
}


}

#endif

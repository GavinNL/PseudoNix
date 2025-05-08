#ifndef PSEUDONIX_SHELL2_H
#define PSEUDONIX_SHELL2_H

#include <map>

#include "System.h"
#include "defer.h"
#include <ranges>
#include "Shell2.h"

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
    E.front().in = in;
    E.back().out = out;

    for(auto & e : E)
    {
        if(e.env.size() > 0 && e.args.size() == 0)
        {
            e.args.push_back("");
        }
        e.queue = proc->queue_name;
    }

    auto pids = proc->executeSubProcess(E);
    auto me = proc->get_pid();//exported_environment->shellPID;
    auto my_cwd = proc->system->getProcessControl(me)->cwd;
    for(auto p : pids)
    {
        if(p != invalid_pid)
            proc->system->getProcessControl(p)->chdir(my_cwd);
    }
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

        if(sub == "$?" || sub == "$!" || sub == "${" || (sub[0]=='$' && std::isalpha(sub[1])) )
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




//===========================
Generator< std::optional<std::string> > BashTokenizerGen2(std::shared_ptr<System::stream_type> in)
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
                co_yield std::string(";");
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

using WhatToDo = std::variant< std::vector<System::pid_type>, int >;


Generator<WhatToDo> parse_if(Generator<std::optional<std::string>> & gen,
                             Generator<std::optional<std::string>>::iterator & a,
                             System::ProcessControl * proc,
                             std::shared_ptr<System::stream_type> in,
                             std::shared_ptr<System::stream_type> out);

Generator<WhatToDo> parse_pipeline(std::string cmd,  Generator<std::optional<std::string>> & gen,
                                                 Generator<std::optional<std::string>>::iterator & a,
                                                 System::ProcessControl * proc,
                                                 std::shared_ptr<System::stream_type> in,
                               std::shared_ptr<System::stream_type> out);

Generator<WhatToDo> parse_block(Generator<std::optional<std::string>> & gen,
                                Generator<std::optional<std::string>>::iterator & a,
                                System::ProcessControl * proc,
                                std::shared_ptr<System::stream_type> in,
                                std::shared_ptr<System::stream_type> out,
                                bool execute_block)
{
    while(a != gen.end())
    {
        auto tok_opt = *a;

        if(!tok_opt.has_value())
        {
            co_yield 0; // wait for more
            ++a;
        }
        else
        {
            auto & tok = *tok_opt;
            if(tok == "done" || tok == "fi" || tok == "else" || tok == "elif")
            {
                break;
            }
            else if( tok == ";" )
            {
                // skip
                ++a;
            }
            else if( tok == "if" )
            {
                auto if_cmd = parse_if(gen, a, proc, in, out);
                for(auto c : if_cmd)
                {
                    co_yield c;
                }
            }
            else
            {
                if(execute_block)
                {
                    auto parse_cmd = parse_pipeline(tok, gen, a, proc, in, out);
                    for(auto c : parse_cmd)
                    {
                        co_yield c;
                    }
                }
                else
                {
                    ++a;
                }
            }
        }
    }
    std::cout << "-- End of Block -- " << std::endl;

    co_return;
}


Generator<WhatToDo> parse_condition(Generator<std::optional<std::string>> & gen,
                             Generator<std::optional<std::string>>::iterator & a,
                             System::ProcessControl * proc,
                             std::shared_ptr<System::stream_type> in,
                             std::shared_ptr<System::stream_type> out,
                             bool evaluate_condition)
{
    (void)gen;
    auto tok = *a;

    std::vector<std::string> condition;

    while("then" != tok)
    {
        tok = *a;
        if(!tok.has_value())
        {
            co_yield 0;
            continue;
        }
        condition.push_back(*tok);
        ++a;
    }

    // condition = {"if", "[[", ..... , "]]" }
    std::cout << std::format("Condition: {}", join(condition)) << std::endl;

    ++a;
    if((*a).has_value() && (*a).value() == ";")
    {
        ++a;
    }
    if((*a).has_value() && (*a).value() == "then")
    {
        ++a;
    }

    condition = std::vector(condition.begin()+1, condition.end()-1);
    if(condition.front() == "[[")
    {
        condition.front() = "test";
    }

    if(condition.back() == ";")
        condition.pop_back();
    if(condition.back() == "]]")
        condition.pop_back();

    {
        if(condition.back() == ";")
        {
            condition.pop_back();
        }
        for(auto & v : condition)
        {
            v = var_sub1(v, proc->env);
        }

        if(evaluate_condition)
        {
            auto subs = execute_pipes(condition, proc, in, out);
            auto ret_code = proc->system->getProcessExitCode(subs[0]);
            co_yield subs;
            proc->env["?"] = std::format("{}",*ret_code);
        }
    }
}

Generator<WhatToDo> parse_if(Generator<std::optional<std::string>> & gen,
                             Generator<std::optional<std::string>>::iterator & a,
                             System::ProcessControl * proc,
                             std::shared_ptr<System::stream_type> in,
                             std::shared_ptr<System::stream_type> out)
{

    assert(*a == "if");

    std::string condition_ret_code = "0";

    // Profess the first if-statement
    {
        // Read and evaluate the condition
        //
        auto cond = parse_condition(gen, a, proc, in, out, true);
        for(auto c : cond)
        {
            co_yield c;
        }
        condition_ret_code = proc->env["?"];

        // Read the block and evaluate it if the condition_ret_code == "0"
        auto block = parse_block(gen, a, proc, in, out, condition_ret_code == "0");
        for(auto c : block)
        {
            co_yield c;
        }
    }
    bool success = condition_ret_code == "0";
    auto next = *a;

    while(next == "elif")
    {
        // parse the condition, and evaluate it it
        // only if the prior condition_ret_code != "0"
        if(!success)
        {
            auto cond = parse_condition(gen, a, proc, in, out, true);
            for(auto c : cond)
            {
                co_yield c;
            }
            condition_ret_code = proc->env["?"];

            auto block = parse_block(gen, a, proc, in, out, condition_ret_code == "0");
            for(auto c : block)
            {
                co_yield c;
            }
            success = condition_ret_code == "0";
        }
        else
        {
            auto cond = parse_condition(gen, a, proc, in, out, false);
            for(auto c : cond)
            {
                co_yield c;
            }
            condition_ret_code = proc->env["?"];
            auto block = parse_block(gen, a, proc, in, out, false);
            for(auto c : block)
            {
                co_yield c;
            }
        }
        next = *a;
    }

    if(next == "else")
    {
        ++a;
        auto else_block = parse_block(gen, a, proc, in, out, condition_ret_code != "0");
        for(auto c : else_block)
        {
            co_yield c;
        }
    }

    next = *a;
    assert(next.value() == "fi");
    ++a;

    co_return;
}

inline
Generator<WhatToDo> parse_pipeline(std::string cmd1,
                                   Generator<std::optional<std::string>> & gen,
                                   Generator<std::optional<std::string>>::iterator & a_it,
                                   System::ProcessControl * proc,
                                   std::shared_ptr<System::stream_type> _in,
                                   std::shared_ptr<System::stream_type> _out)
{
    (void)cmd1;
    std::vector<std::string> args;

    while(a_it != gen.end())
    {
        auto a = *a_it;
        ++a_it;
        if(a == ";" || a == "\n")
            break;
        if(!a.has_value())
        {
            co_yield 0;
            continue;
        }
        args.push_back(*a);
    }

    // we need to execute the command here and wait for
    args.erase(std::find_if(args.begin(), args.end(), [](auto const & s)
                            {
                                return s.size() && s.front() == '#' ;
                            }), args.end());

    if(!args.empty())
    {      
        for(auto & v : args)
        {
            v = var_sub1(v, proc->env);
        }

        {
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
                    newargs.push_back("sh");

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
            auto pids = execute_pipes( {"sh", "--noprofile"}, proc, STDIN, _out);
#endif
            *_out << std::format("{}\n", pids[0]);
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

            auto subProcess = execute_pipes( std::vector(cmd.begin()+1, cmd.end()), proc, _in, _out);

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
                proc->env["?"] = std::to_string(ret_value);
            }

            op_args.pop_back();
        }
    }
    co_return;
}
//===========================



inline System::task_type shell_coro(System::e_type ctrl)
{
    PSEUDONIX_PROC_START(ctrl);

    std::string _current;
    std::string script = "";
    int ret_value = 0;

    ctrl->exported["SHELL_PID"] = true;
    ctrl->env["SHELL_PID"] = std::to_string(PID);

    auto _in  = ctrl->in;
    auto _out = ctrl->out;

    //===========================================================================
    // Parse arguments. Probably should use an external library for this
    // but didn't want to add the dependnecy
    //===========================================================================
    auto _args = ARGS;
    auto no_profile = std::find(_args.begin(), _args.end(), "--noprofile");
    bool load_etc_profile = true;
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
        script += SYSTEM.file_to_string("/etc/profile");
    }

    if(_args.size() > 1)
    {
        if(SYSTEM.exists(_args[1]))
        {
            script = SYSTEM.file_to_string(_args[1]);
            script += "\nexit;";
        }
        else
        {
            COUT << std::format("{}: {}: no such file or directory\n", ARGS[0], _args[1]);
        }
    }

    *_in << script;

    auto & EXIT_SHELL = ENV["EXIT_SHELL"];

    auto gen = BashTokenizerGen2(_in);
    auto a_it = gen.begin();

    auto block = parse_block(gen, a_it, ctrl.get(), _in, _out, true);

    //while(EXIT_SHELL.empty())
    {
        auto it = block.begin();
        while(it != block.end())
        {
            auto _cc = *it;
            if( std::holds_alternative<int>(_cc) && std::get<int>(_cc) == 0)
            {
                HANDLE_AWAIT_TERM( co_await ctrl->await_has_data(_in), ctrl);
                ++it;
            }
            else if( std::holds_alternative<std::vector<System::pid_type>>(_cc))
            {
                HANDLE_AWAIT_TERM(co_await ctrl->await_finished(std::get<std::vector<System::pid_type>>(_cc)), ctrl);
                ++it;
            }

            if(!EXIT_SHELL.empty())
                co_return 0;
        }
    }

    if(std::errc() != std::from_chars(ENV["?"].data(), ENV["?"].data() + ENV["?"].size(), ret_value).ec)
    {
        co_return 1;
    }

    co_return std::move(ret_value);
}


}

#endif

#ifndef MINILINUX_SHELL_H
#define MINILINUX_SHELL_H

#include <map>
#include <future>
#include <fmt/format.h>
#include "MiniLinux.h"

namespace bl
{

struct AstNode
{
    std::string value;
    std::shared_ptr<AstNode> left;
    std::shared_ptr<AstNode> right;

    AstNode(std::string const &s) : value(s){}
};

struct Tokenizer
{
    std::string_view input;

    std::string current;
    std::string _next;
    size_t pos = 0;

    std::string next() {

        if(!_next.empty())
        {
            auto s = std::move(_next);
            return s;
        }
        while(pos < input.size())
        {
            char c = input[pos];

            auto sub = input.substr(pos, 2);
            if (sub == "&&" || sub == "||") {
                pos += 2;
                if (!current.empty()) {
                    auto s = std::move(current);
                    _next = sub;
                    return s;
                }
            } else {
                if(std::isspace(c))
                {
                    if(!current.empty())
                        current += c;
                }
                else
                {
                    current += c;
                }
                ++pos;
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


std::shared_ptr<AstNode> generateTree(std::string_view cmdline)
{
    bl::Tokenizer T;
    T.input = cmdline;

    std::shared_ptr<AstNode> left  = std::make_shared<AstNode>(T.next());
    std::shared_ptr<AstNode> op    = std::make_shared<AstNode>(T.next());
    std::shared_ptr<AstNode> right = std::make_shared<AstNode>(T.next());

    if(!left->value.empty() && op->value.empty() && right->value.empty())
    {
        return left;
    }
    auto top = op;

    op->left = left;
    op->right = right;

    while(true)
    {
        auto s = T.next();
        if(s.empty()) break;
        if(s == "&&" || s == "||")
        {
            right->left  = std::make_shared<AstNode>(right->value);
            right->right = std::make_shared<AstNode>(T.next());
            right->value = s;

            right = right->right;
        }
    }

    return top;
}


auto parse_command_line(std::string_view command, std::shared_ptr<MiniLinux::stream_type> in={}, std::shared_ptr<MiniLinux::stream_type> out={}) {
    std::vector<MiniLinux::Exec> E;
    std::vector<std::string> args;
    std::string current;
    bool in_single_quote = false;
    bool in_double_quote = false;

    for (size_t i = 0; i < command.size(); ++i) {
        char c = command[i];

        // Handle escape sequences
        if (c == '\\' && i + 1 < command.size()) {
            current += command[++i];  // Skip the backslash and add the next character
        }
        else if (c == '\'' && !in_double_quote) {
            in_single_quote = !in_single_quote;
        }
        else if (c == '"' && !in_single_quote) {
            in_double_quote = !in_double_quote;
        }
        else if (std::isspace(c) && !in_single_quote && !in_double_quote) {
            if (!current.empty()) {
                args.push_back(current);
                current.clear();
            }
        }
        else if (c=='|' && !in_single_quote && !in_double_quote) {
            auto &_exec = E.emplace_back();
            _exec.args = std::move(args);
        }
        else {
            current += c;
        }
    }

    // Add the last argument if any
    if (!current.empty()) {
        args.push_back(current);
    }
    if(!args.empty())
    {
        auto &_exec = E.emplace_back();
        _exec.args = std::move(args);
    }


    E[0].in = in;
    E.back().out = out;
    for(size_t j=0;j<E.size()-1;j++)
    {
        E[j].out = MiniLinux::make_stream();
        E[j+1].in = E[j].out;
    }

    return E;
}

/**
 * @brief shell
 * @param exev
 * @param S
 * @param L
 * @return
 *
 * The shell() command acts like the Linux's sh command. It reads commands
 * from the input stream and executes commands.
 *
 * Each command that get excectued is itself a coroutine. These coroutine tasks
 * will need to be placed on a scheduler. That's what the SchedulerFunction does.
 *
 * The SchedulerFunction is a functional object with opertor() overriden
 * It takes a
 */
template<typename SchedulerFunction>
MiniLinux::task_type shell(MiniLinux::Exec exev, SchedulerFunction S, MiniLinux & L)
{
    //;bool quoted = false;

    //std::vector<MiniLinux::Exec> E;

    //char last_char = 0;
    //auto next_arg  = [&](){if(E.back().args.back().size() != 0) E.back().args.emplace_back();};
    //auto next_exec = [&](){E.emplace_back(); E.back().args.emplace_back();};
    //auto last_arg = [&]() -> std::string& {return E.back().args.back();};
    //auto push_arg_char = [&](char c) {
    //    last_arg().push_back(c);
    //    last_char = c;
    //};
    //auto pop_char = [&]()
    //{
    //    last_arg().pop_back();
    //};
    //(void)pop_char;

    //(void)last_arg;
    //next_exec();
    //next_arg();



    auto _execute = [&L, &S](std::string const & cmdline, auto _in, auto _out) -> gul::Task_t<int>
    {
        auto E = parse_command_line(cmdline, _in, _out);

        std::vector<std::future<int>> _returnValues;

        for(auto & e : E)
        {
            auto it = L.funcs.find(e.args[0]);
            //std::cout << "Executing: " << e.args[0] << std::endl;
            if(it != L.funcs.end())
            {
                _returnValues.emplace_back(S(it->second(e)));
            }
        }

        size_t count = 0;
        while(true)
        {
            // check each of the futures for their completion
            for(size_t i=0;i<_returnValues.size();i++)
            {
                auto & f = _returnValues[i];

                if(f.valid())
                {
                    if(f.wait_for(std::chrono::seconds(0))==std::future_status::ready)
                    {
                        ++count;
                        f.get();
                        if(E[i].out && i!=_returnValues.size()-1)
                            E[i].out->close();
                    }
                }
            }

            if(static_cast<size_t>(count) == _returnValues.size())
            {
                //std::cout << "Finished Executing: "<<std::endl;
                break;
            }
            else
            {
                //std::cout << "Suspending "<<std::endl;
                co_await std::suspend_always{};
            }
        }
        //std::cout << "Finished Executing: "<<std::endl;
        co_return 0;
    };

    std::string _current;
    while(!exev.in->eof() )
    {
        while(!exev.in->has_data())
        {
            //std::cout << "waiting on input" << std::endl;
            co_await std::suspend_always{};
        }
        auto c = exev.in->get();
        _current += c;
        if(c != ';' && c != '\n')
        {
            continue;
        }
        _current.pop_back();

        std::cout << "Read: "<< _current << std::endl;
        //auto vv = parse_command_line(_current);
        //std::cout << fmt::format("{}", fmt::join(vv,",")) << std::endl;
        auto top = generateTree(_current);

        auto t = top;
        {
            if(t->value == "&&")
            {

            }
            else if(t->value == "||")
            {

            }
            else
            {
                auto _task = _execute(t->value, exev.in, exev.out);
                while(!_task.done())
                {
                    _task.resume();
                    //std::cout << "Bash suspending" << std::endl;
                    co_await std::suspend_always{};
                }

            }
        }

        _current.clear();
    }
    std::cout << "Exited" << std::endl;
    co_return 0;
#if 0
    while(!exev.in->eof() )
    {
        while(!exev.in->has_data())
        {
            //std::cout << "waiting on input" << std::endl;
            co_await std::suspend_always{};
        }
        auto c = exev.in->get();

        if(quoted)
        {
            push_arg_char(c);
        }
        else
        {
            if(c == ' ')
            {
                if(last_char != ' ')
                {
                    next_arg();
                }
            }
            else
            {
                push_arg_char(c);
            }
        }

        switch(c)
        {
        case '\\':
            c = exev.in->get();
            push_arg_char(c);
            break;
        case ' ':
            if(quoted)
                push_arg_char(c);
            else
            {
                next_arg();
            }
            break;
        case '"':
            quoted = !quoted;
            break;
        case '|':
            if(!quoted)
            {
                next_exec();
            }
            break;
        case ';':
        case '\n':
        {
            std::cout << "End of line: " << E[0].args.size() << std::endl;
            E[0].in = exev.in;
            E.back().out = exev.out;

            // make sure each executable's output
            // is passed to the next's input
            for(size_t j=0;j<E.size()-1;j++)
            {
                E[j].out = MiniLinux::make_stream();
                E[j+1].in = E[j].out;
            }

            std::vector<std::future<int>> _returnValues;
            for(size_t j=0;j<E.size();j++)
            {
                auto it = L.funcs.find(E[j].args[0]);
                std::cout << "Executing: " << E[j].args[0] << std::endl;
                if(it != L.funcs.end())
                {

                    auto new_task = it->second(E[j]);
                    _returnValues.emplace_back(S(std::move(new_task)));
                }
            }

            size_t count=0;
            while(true)
            {
                // check each of the futures for their completion
                for(size_t i=0;i<_returnValues.size();i++)
                {
                    auto & f = _returnValues[i];
                    if(f.valid())
                    {
                        if(f.wait_for(std::chrono::seconds(0))==std::future_status::ready)
                        {
                            ++count;
                            f.get();
                            if(E[i].out)
                                E[i].out->close();
                        }
                    }
                }

                if(static_cast<size_t>(count) == _returnValues.size())
                {
                    break;
                }
                else
                {
                    co_await std::suspend_always{};
                }
            }
            E.clear();
            next_exec();
            next_arg();
        }

        // execute E
        break;
        default:
            push_arg_char(c);
            break;
        }
    }
#endif
    std::cout << "Exit" << std::endl;
    co_return 1;
}



}
#endif

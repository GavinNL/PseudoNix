#ifndef MINILINUX_SHELL_H
#define MINILINUX_SHELL_H

#include <map>
#include <future>
#include <fmt/format.h>
#include "MiniLinux.h"

namespace bl
{

template <typename Range>
std::string join(const Range& elements, const std::string& delimiter) {
    std::ostringstream oss;
    bool first = true;
    for (const auto& element : elements) {
        if (!first) {
            oss << delimiter;
        }
        oss << element;
        first = false;
    }
    return oss.str();
}

struct AstNode
{
    std::string value;
    std::shared_ptr<AstNode> left;
    std::shared_ptr<AstNode> right;

    AstNode(std::string const &s) : value(s){}
};

void printAST(std::shared_ptr<AstNode> d, std::string indent)
{
    std::cout << indent << d->value << std::endl;;
    if(d->left)
        printAST(d->left, indent + "  ");
    if(d->right)
        printAST(d->right, indent + "  ");
}

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


auto parse_command_line(std::string_view command, std::shared_ptr<MiniLinux::stream_type> in={}, std::shared_ptr<MiniLinux::stream_type> out={})
{
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


auto ast_execute(std::shared_ptr<bl::AstNode> node, auto & L, auto _in, auto _out) -> gul::Task_t<int>
{

    if(node->value == "&&" || node->value == "||")
    {
        auto & left = node->left;
        auto & right = node->right;

        auto left_task = ast_execute(left, L, _in, _out);
        while(!left_task.done())
        {
            left_task.resume();
            co_await std::suspend_always{};
        }

        auto ret = left_task();

        if(node->value == "||" && ret == 0)
        {
            co_return ret;
        }
        else if(node->value == "&&" && ret != 0)
        {
            co_return ret;
        }

        auto right_task = ast_execute(right, L, _in, _out);
        while(!right_task.done())
        {
            right_task.resume();
            co_await std::suspend_always{};
        }

        co_return right_task();
    }



    bool in_background = false;

    auto in_stream = _in;
    if(node->value.back() == '&')
    {
        node->value.pop_back();
        in_background = true;
        in_stream = MiniLinux::make_stream();
        in_stream->close();
    }

    auto E = parse_command_line(node->value,
                                in_stream,
                                _out);


#if 0
    std::vector<MiniLinux::task_type> taskList;
    for(auto & e : E)
    {
        //std::cout << "Exec: " << std::format("{}", fmt::join(e.args, ", ")) << std::endl;
        //std::cout << "  in: " << e.in << std::endl;
        //std::cout << " out: " << e.out << std::endl;
        taskList.emplace_back(L.runRawCommand(e));
    }

    int retcode = 0;
    while(true)
    {
        uint32_t count = 0;
        for(size_t i=0;i<E.size();i++)
        {
            auto & T = taskList[i];

            if(T.done())
            {
                retcode = T();
                count++;
                // we need to close the output stream
                // so that the input for the next stage
                // will return a proper eof()
            }
            else
            {
                T.resume();
                co_await std::suspend_always{};
            }
        }
        if(count == taskList.size())
            break;
        co_await std::suspend_always{};
    }
    co_return retcode;
#else
    std::vector<std::future<int>> _returnValues;

    for(auto & e : E)
    {
#if 1
        _returnValues.emplace_back( L.system(e));
#else
        auto it = L.funcs.find(e.args[0]);

        if(it != L.funcs.end())
        {
            //std::cout << "Found: " << e.args[0] << std::endl;
            _returnValues.emplace_back(S(it->second(e)));
        }
        else
        {
            co_return 127;
        }
#endif
    }

    size_t count = 0;
    int _retval = 0;

    while(!in_background)
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
                    _retval = f.get();
                    if(E[i].out && i!=_returnValues.size()-1)
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
    co_return _retval;
#endif
};

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
 *
 */
MiniLinux::task_type shell(MiniLinux::Exec exev)
{
    std::string _current;
    static int count = 0;
    count++;

    int shell_number = count;


    exev << "-------------------------\n";
    exev << std::format("Welcome to shell: {}\n",shell_number);
    exev << "-------------------------\n";
    exev << std::format("{}", exev.env["PROMPT"]);

    while(!exev.is_sigkill() && !exev.in->eof())
    {
        while(!exev.in->has_data() && !exev.is_sigkill())
        {
            co_await std::suspend_always{};
        }
        auto c = exev.in->get();
        _current += c;
        if(c != ';' && c != '\n')
        {
            continue;
        }
        _current.pop_back();

        if(_current.empty())
            continue;

        auto top = generateTree(_current);
        if(top->value == "exit")
        {
            break;
        }
        if(top->value == "count")
        {
            std::cout << shell_number << std::endl;
        }


        {            

            // If we are not runing in the background (eg: we dont have the & at the end)
            // we will execute the task and then wait for it
            auto _task = ast_execute(top, *exev.mini, exev.in, exev.out);

            // Wait for the task to complete
            while(!_task.done() && !exev.is_sigkill())
            {
                _task.resume();
                co_await std::suspend_always{};
            }
            exev << std::format("{}", exev.env["PROMPT"]);
        }

        //std::cout << "\n" << shell_number << "> " << std::flush;
        _current.clear();
    }
    exev << std::format("Exiting Shell {}\n", shell_number);
    //std::cout << "Exited" << std::endl;
    co_return 0;
}



}
#endif

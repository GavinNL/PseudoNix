#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

//#define PSEUDONIX_LOG_LEVEL_SYSTEM
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


Generator<std::optional<std::vector<std::string>>> bash_line_generator2(std::shared_ptr<System::stream_type> s_in)
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
            if(line_args.front() == "elif")
            {
                std::vector<std::string> _else = {"else"};
                co_yield _else;
                line_args[0] = "if";
            }

            co_yield line_args;
            line_args.clear();
        }
    }
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
            co_yield line_args;
            line_args.clear();
        }
    }
}

using WhatToDo3 = std::variant< std::string,                  // queue to hop onto
                                std::vector<System::pid_type>,  // PIDS-to wait on
                                int                             // 0 - do nothing
                               >;


template<typename T>
std::span< T> pop_front(std::span<T> & sp, int count=1)
{

    return sp( sp.begin()+count, sp.end());
}

Generator<WhatToDo3> execute_if(std::vector< std::vector<std::string> > & if_block, System::ProcessControl * proc)
{
    int if_count = 1;
    while(if_count != 0 && if_block.back()[0] != "fi")
    {
        co_yield 0;
        if(if_block.back().front() == "if") ++if_count;
        if(if_block.back().front() == "fi") --if_count;
    }

    assert( if_block.front()[0] == "if");
    assert( if_block.back()[0]  == "fi");

    //
    //  if_block should now contain one f
    //
    //
    std::string last_exit_code = "0";
    auto condition_args = std::vector(if_block.front().begin()+1, if_block.front().end());
    {
        auto E = proc->system->parseArguments(condition_args);
        E.in = proc->in;
        E.out = proc->out;
        auto pid = proc->system->runRawCommand(E, proc->get_pid());
        std::vector<System::pid_type> pp;
        pp.push_back(pid);
        co_yield pp;

        last_exit_code = proc->env["?"];
    }
    //
    //
    if(last_exit_code == "0" )
    {
        assert(if_block[1].front() == "then");

        // find the closing else block, or closing fi
        size_t count = 1;
        for(size_t i=0;i<if_block.size();i++)
        {
            if(if_block[i].front() == "if") count++;
            if(if_block[i].front() == "fi") count--;
            if(if_block[i].front() == "else" && count == 1)
            {

                // we found the end if the if block

            }
        }

        // execute the blocok
    }
    else
    {
        // execute the else-block
    }
    // for(auto & v : if_block)
    // {
    //     std::cout << std::format("{}", join(v)) << std::endl;
    // }
    (void)proc;
    co_return;
}


Generator<WhatToDo3> process_command(std::vector<std::string> args, System::ProcessControl * proc)
{
    auto E = proc->system->parseArguments(args);
    E.in = proc->in;
    E.out = proc->out;
    auto pid = proc->system->runRawCommand(E, proc->get_pid());
    auto ex = proc->system->getProcessExitCode(pid);
    std::vector<System::pid_type> pp;
    pp.push_back(pid);
    co_yield pp;
}
Generator<WhatToDo3> process_block(std::vector< std::vector<std::string> > script, System::ProcessControl * proc);

Generator<WhatToDo3> process_if(std::vector< std::vector<std::string> > script, System::ProcessControl * proc)
{
    assert(script.front().front() == "if");
    //for(size_t i=0; i < script.size(); i++)
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
            //if(false)
            {
                // regular command
                for(auto c : process_command(condition, proc))
                {
                    auto ex = proc->system->getProcessExitCode( std::get<std::vector<System::pid_type>>(c).back() );
                    co_yield c;
                    exit_code = *ex;
                    break;
                }

                if(exit_code == 0)
                {
                    auto block = std::vector(block_script.begin()+2, block_script.end());
                    for(auto cc : process_block(block, proc))
                    {
                        co_yield cc;
                    }
                }
            }
        }


        assert(first_it != script.end());
        if(first_it->front() == "fi")
            break;
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
            for(auto c : process_if(if_statement, proc))
            {
                co_yield c;
            }
            i = j+1;
        }
        else
        {
            for(auto c : process_command(script[i], proc))
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


    std::vector< std::vector<std::string> > script;
    int if_count=0;

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

        if(if_count != 0)
        {


        }
        else
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
                    HANDLE_AWAIT_INT_TERM( co_await ctrl->await_finished(pids_to_wait_on), ctrl);
                    ctrl->out->_eof = false;
                    ctrl->env["?"] = std::to_string(*exit_code_p);
                }
            }
            script.clear();
        }
        ++a_it;
    }
    co_return 0;
}

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

#if 1
SCENARIO("Tokenizer Generator")
{
    std::string script = R"foo(
if false; then
    echo if
elif true; then
    echo elif
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

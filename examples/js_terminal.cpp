#include "common_setup.h"
#include <PseudoNix/System.h>
#include <emscripten.h>
#include <stdio.h>

int counter = 0;

namespace pn = PseudoNix;

pn::System sys;
pn::System::pid_type sh_id = pn::invalid_pid;
std::shared_ptr<pn::System::stream_type> sh_in;
std::shared_ptr<pn::System::stream_type> sh_out;

extern "C" {
//
static bool is_init = false;

// These are exported by emscripten
void init()
{
    if (!is_init) {
        is_init = true;
        counter = 0;
        setup_functions(sys);
    }
    if (!sys.isRunning(sh_id)) {
        sh_id = sys.spawnProcess({"sh"});
        std::tie(sh_in, sh_out) = sys.getIO(sh_id);
    }
}

/**
 * @brief input
 * @param c
 * 
 * Input a character into the shell
 */
void input(char c)
{
    init();
    sh_in->put(c);
}

char const *update()
{
    init();
    static std::string out = "";
    sys.taskQueueExecute("MAIN");
    out = sh_out->str();
    return out.c_str();
}
}

#if !defined __EMSCRIPTEN__
int main(int argc, char **argv)
{
    init();
    return 0;
}
#endif

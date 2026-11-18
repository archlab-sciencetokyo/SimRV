#include "simrv/util/ShellCompletion.hpp"

namespace simrv::util {

auto generate_shell_completion(std::string_view shell) -> std::string {
    if (shell == "zsh") {
        return R"(#compdef simrv
_simrv() {
    local -a commands
    commands=(
        'run:Execute a RISC-V image'
        'mission:Load a classroom mission in the TUI'
        'config:Export SoC configuration data'
        'explain:Explain an instruction or ISA feature'
        'doctor:Inspect the environment'
        'help:Show help'
    )
    _arguments -C '1:command:->command' '*::argument:_files'
    case $state in
        command) _describe 'simrv command' commands ;;
        argument)
            if [[ $line[1] == config ]]; then
                _arguments '1:(export-soc)' '*:preset or file:_files'
            else
                _files
            fi
            ;;
    esac
}
_simrv "$@"
)";
    }
    if (shell == "fish") {
        return R"(# fish completion for simrv runtime commands
complete -c simrv -f
complete -c simrv -n "__fish_use_subcommand" -a "run" -d "Execute a RISC-V image"
complete -c simrv -n "__fish_use_subcommand" -a "mission" -d "Load a classroom mission in the TUI"
complete -c simrv -n "__fish_use_subcommand" -a "config" -d "Export SoC configuration data"
complete -c simrv -n "__fish_use_subcommand" -a "explain" -d "Explain an instruction or ISA feature"
complete -c simrv -n "__fish_use_subcommand" -a "doctor" -d "Inspect the environment"
complete -c simrv -n "__fish_use_subcommand" -a "help" -d "Show help"
complete -c simrv -n "__fish_seen_subcommand_from config" -a "export-soc"
complete -c simrv -s h -l help -d "Show help"
complete -c simrv -s m -l image -r -d "Load guest image"
complete -c simrv -l profile -r -d "Select RISC-V architecture profile"
)";
    }
    return R"(# bash completion for simrv runtime commands
_simrv() {
    local cur prev words cword
    _init_completion || return
    local commands="run mission config explain doctor help"
    if [[ $cword -eq 1 ]]; then
        COMPREPLY=($(compgen -W "$commands" -- "$cur"))
        return 0
    fi
    if [[ ${words[1]} == config && $cword -eq 2 ]]; then
        COMPREPLY=($(compgen -W "export-soc" -- "$cur"))
    else
        COMPREPLY=($(compgen -f -- "$cur"))
    fi
}
complete -F _simrv simrv
)";
}

}  // namespace simrv::util

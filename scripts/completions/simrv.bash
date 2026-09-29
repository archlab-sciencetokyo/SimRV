_simrv_complete() {
    local cur=${COMP_WORDS[COMP_CWORD]}
    local opts='run tui inspect explain attach --help --version --license --quiet --verbose'
    COMPREPLY=( $(compgen -W "$opts" -- "$cur") )
}
complete -F _simrv_complete simrv

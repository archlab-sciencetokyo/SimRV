_simrv_complete() {
    local cur=${COMP_WORDS[COMP_CWORD]}
    local opts='--help --version --license --cli --tui --os --isa --steps --max-steps --entry --log-file --json-summary --log-level --quiet --verbose --gdb --gdb-port --disk --fdt --dtb --platform --harts --vlen --dram-size --net --trace --instmix'
    COMPREPLY=( $(compgen -W "$opts" -- "$cur") )
}
complete -F _simrv_complete simrv

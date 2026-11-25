# bash completion for ctron
#
# Installed as <datadir>/bash-completion/completions/ctron, where
# bash-completion loads it the first time ctron is completed; sourcing
# it by hand works too (the guard at the bottom registers the function).
#
# Mode, snapshot and fan-curve names are read from the config directory
# rather than from `ctron mode list` and friends: those initialise the
# hardware layer (an nvidia-smi query, which can wake the GPU) and
# create files on a fresh machine (modes.ini, profiles/) — pressing Tab
# must do neither. The lists mirror src/modes.c and src/profile.c; the
# values mirror `ctron --help` and src/cmds.c.

_ctron_config_dir() {
    printf '%s\n' "${CTRON_CONFIG:-${XDG_CONFIG_HOME:-$HOME/.config}/ctron}"
}

# _ctron_list modes|snapshots|curves — one name per line.
_ctron_list() {
    local cfg dir ext f name
    cfg=$(_ctron_config_dir)
    case $1 in
    modes)
        # a section counts once it has a `steps =` line, and ctron stores
        # at most 23 characters of its name
        [[ -r $cfg/modes.ini ]] || return 0
        awk '
            /^[[:space:]]*\[/ { s = $0; sub(/^[[:space:]]*\[/, "", s); sub(/\].*/, "", s); next }
            /^[[:space:]]*[#;]/ { next }
            tolower($0) ~ /^[[:space:]]*steps[[:space:]]*=/ {
                if (s != "" && !seen[s]++) print substr(s, 1, 23)
            }' "$cfg/modes.ini"
        return 0
        ;;
    snapshots) dir=$cfg/profiles ext=.ctr ;;
    curves)    dir=$cfg/fan_curves ext=.fc ;;
    *) return 1 ;;
    esac
    for f in "$dir"/*"$ext" "$dir"/*/*"$ext"; do
        [[ -f $f ]] || continue
        name=${f#"$dir"/}
        printf '%s\n' "${name%"$ext"}"
    done
}

_ctron() {
    local cur prev flag w word sub fanword
    cur=${COMP_WORDS[COMP_CWORD]}
    prev=${COMP_WORDS[COMP_CWORD-1]}
    fanword=${COMP_WORDS[COMP_CWORD-2]}
    COMPREPLY=()

    # --flag=value completes the value of that flag
    flag=
    if [[ $cur == --*?=* ]]; then
        flag=${cur%%=*}
        cur=${cur#*=}
    fi

    local -a flags
    flags=(--help -h --version -V --tui -t --status -s --watch -w --doctor --csv
           --follow --daeboard-start --daeboard-stop --daeboard-reload --json
           --dry-run --config-dir --apply-startup --mode --profile --ac-profile
           --battery-profile --epp --freq --hz --battery --battery-oneshot --ppt
           --nv-boost --nv-temp --gpu-clock --panel-od --cpu-boost --kbd --aura
           --fan --fan-curve --fan-write)

    # first word that is neither a modifier nor the value of --config-dir
    local i
    word=
    for ((i = 1; i < COMP_CWORD; i++)); do
        w=${COMP_WORDS[i]}
        if [[ $w == --config-dir ]]; then
            ((i++))
        elif [[ $w != --* ]]; then
            word=$w
            break
        fi
    done

    local values
    case $word in
    mode|snapshot|profile|fancurve|fan-curve)
        local subs names sub= j skip=0 take_name list
        case $word in
        mode) subs='list show add delete'; names=modes ;;
        snapshot|profile) subs='list apply diff export delete'; names=snapshots ;;
        *) subs='list apply diff export delete'; names=curves ;;
        esac
        # the subcommand: the next word that is not a global modifier
        for ((j = i + 1; j < COMP_CWORD; j++)); do
            w=${COMP_WORDS[j]}
            if (( skip )); then
                skip=0
            elif [[ $w == --config-dir ]]; then
                skip=1
            elif [[ $w != --json && $w != --dry-run ]]; then
                sub=$w
                break
            fi
        done
        if (( j == COMP_CWORD )); then
            list=$subs
        elif [[ $cur == -* ]]; then
            # the global modifiers work everywhere; --dry-run is offered only
            # where it does something (delete would act for real under it)
            list='--json --config-dir'
            [[ $word != mode && $sub == apply ]] && list="$list --dry-run"
        else
            if [[ $word == mode ]]; then
                [[ $sub == show || $sub == delete ]] && take_name=1
            else
                [[ $sub == apply || $sub == diff || $sub == export || $sub == delete ]] && take_name=1
            fi
            [[ ${take_name-} ]] && list=$(_ctron_list "$names")
        fi
        COMPREPLY=($(compgen -W "${list:-}" -- "$cur"))
        return 0
        ;;
    esac

    # second words: "--fan cpu|gpu <on|off>" and "--aura <effect> [color]"
    if [[ -z $flag && ( $prev == cpu || $prev == gpu ) && $fanword == --fan ]]; then
        COMPREPLY=($(compgen -W 'on off' -- "$cur"))
        return 0
    fi
    if [[ -z $flag && $fanword == --aura ]]; then
        case $prev in
        static|breathe|rainbow-cycle|rainbow-wave|pulse|comet|flash|stars|rain|highlight|laser|ripple)
            COMPREPLY=($(compgen -W 'Ice Red Green Blue Yellow Purple Orange White' -- "$cur"))
            return 0
            ;;
        esac
    fi

    case ${flag:-$prev} in
    --profile) values='quiet balanced performance' ;;
    --ac-profile|--battery-profile) values='off quiet balanced performance' ;;
    --epp) values='power balance_power balance_performance performance' ;;
    --freq) values='core ccd' ;;
    --hz) values='max' ;;
    --battery) values='20 30 40 50 60 70 80 90 100' ;;
    --ppt) values='Q45 B60 P80 45,55,55 60,75,75 80,80,80 off on' ;;
    --nv-boost) values='5 10 15 20 25' ;;
    --nv-temp) values='75 80 85 87' ;;
    --gpu-clock) values='reset' ;;
    --panel-od|--cpu-boost) values='on off' ;;
    --kbd) values='off low med high' ;;
    --mode) values=$(_ctron_list modes) ;;
    --aura) values='static breathe rainbow-cycle rainbow-wave pulse comet flash stars rain highlight laser ripple' ;;
    --fan) values="stock silent cool full on off cpu gpu $(_ctron_list curves)" ;;
    --fan-curve) values='cpu gpu' ;;
    --config-dir|--csv)
        if declare -F _filedir >/dev/null; then
            [[ $prev == --config-dir ]] && _filedir -d || _filedir
        elif [[ $prev == --config-dir ]]; then
            COMPREPLY=($(compgen -d -- "$cur"))
        else
            COMPREPLY=($(compgen -f -- "$cur"))
        fi
        return 0
        ;;
    *) values='' ;;
    esac

    if [[ -n $values ]]; then
        COMPREPLY=($(compgen -W "$values" -- "$cur"))
    elif [[ -z $flag ]]; then
        # the subcommands are stand-alone commands: only the first argument
        local -a candidates=("${flags[@]}")
        [[ -z $word ]] && candidates+=(mode snapshot profile fancurve fan-curve)
        COMPREPLY=($(compgen -W "${candidates[*]}" -- "$cur"))
    fi
    return 0
}

complete -F _ctron ctron

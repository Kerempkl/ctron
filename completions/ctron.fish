# fish completion for ctron. Installed as
# <datadir>/fish/vendor_completions.d/ctron.fish (a user install goes to
# ~/.local/share/fish/vendor_completions.d, which fish loads by default).
#
# Mode, snapshot and fan-curve names are read from the config directory
# rather than from `ctron mode list` and friends: those initialise the
# hardware layer (an nvidia-smi query, which can wake the GPU) and
# create files on a fresh machine (modes.ini, profiles/) — pressing Tab
# must do neither. The lists mirror src/modes.c and src/profile.c; the
# values mirror `ctron --help` and src/cmds.c.

function __ctron_config_dir
    if set -q CTRON_CONFIG
        echo $CTRON_CONFIG
    else if set -q XDG_CONFIG_HOME
        echo $XDG_CONFIG_HOME/ctron
    else
        echo $HOME/.config/ctron
    end
end

function __ctron_names --argument-names kind
    switch $kind
        case modes
            # a section counts once it has a `steps =` line, and ctron
            # stores at most 23 characters of its name
            set -l f (__ctron_config_dir)/modes.ini
            if test -r $f
                awk '
                    /^[[:space:]]*\[/ { s = $0; sub(/^[[:space:]]*\[/, "", s); sub(/\].*/, "", s); next }
                    /^[[:space:]]*[#;]/ { next }
                    tolower($0) ~ /^[[:space:]]*steps[[:space:]]*=/ {
                        if (s != "" && !seen[s]++) print substr(s, 1, 23)
                    }' $f
            end
        case snapshots
            __ctron_files (__ctron_config_dir)/profiles ctr
        case curves
            __ctron_files (__ctron_config_dir)/fan_curves fc
    end
end

function __ctron_files --argument-names dir ext
    set -l n
    for f in $dir/*.$ext $dir/*/*.$ext
        if test -f $f
            set n (string replace -- "$dir/" "" $f)
            string replace --regex "\.$ext\$" "" $n
        end
    end
end

# true until one of the subcommand groups was named
function __ctron_top
    not __fish_seen_subcommand_from mode snapshot profile fancurve fan-curve
end

# true while the line is still just `ctron` plus global modifiers: the
# stand-alone subcommands must be the first argument
function __ctron_first
    __ctron_top; or return 1
    set -l t (commandline --current-process --tokens-expanded --cut-at-cursor)
    set -e t[1]
    set -l skip
    for w in $t
        if set -q skip
            set -e skip
        else if test $w = --config-dir
            set -l skip 1
        else if not contains -- $w --json --dry-run
            return 1
        end
    end
    return 0
end

function __ctron_second --argument-names groups subs
    set -l groups (string split ' ' -- $groups)
    set -l subs (string split ' ' -- $subs)
    __fish_seen_subcommand_from $groups; and not __fish_seen_subcommand_from $subs
end

# __ctron_after a b — true when the two words before the cursor are a and b
function __ctron_after --argument-names a b
    set -l t (commandline --current-process --tokens-expanded --cut-at-cursor)
    test (count $t) -ge 3; and test $t[-2] = $a; and test $t[-1] = $b
end

# __ctron_option_value opt — true when the cursor is on the word right after
# the second word of "--opt <word> [<second word>]" (--aura effect [color])
function __ctron_after_option_value --argument-names opt
    set -l t (commandline --current-process --tokens-expanded --cut-at-cursor)
    test (count $t) -ge 3; and test $t[-2] = $opt
end

# nothing but --csv takes a file
complete -c ctron -f

complete -c ctron -n __ctron_top -s h -l help -d 'print the help text and exit'
complete -c ctron -n __ctron_top -s V -l version -d 'print the version and exit'
complete -c ctron -n __ctron_top -s t -l tui -d 'open the fullscreen TUI'
complete -c ctron -n __ctron_top -s s -l status -d 'print a live hardware snapshot'
complete -c ctron -n __ctron_top -s w -l watch -d 'print rolling one-line telemetry'
complete -c ctron -n __ctron_top -l doctor -d 'print a capability report'
complete -c ctron -n __ctron_top -l csv -r -F -d 'append telemetry rows to a CSV'
complete -c ctron -n __ctron_top -l follow -d 'run the actions of daeboard binds'
complete -c ctron -n __ctron_top -l daeboard-start -d 'start the daeboard daemon'
complete -c ctron -n __ctron_top -l daeboard-stop -d 'ask the daeboard daemon to quit'
complete -c ctron -n __ctron_top -l daeboard-reload -d 'recompile daeboard.binds'
complete -c ctron -n __ctron_top -l apply-startup -d 'apply the configured Boot snapshot'
complete -c ctron -l json -d 'machine-readable output for --status and --doctor'
complete -c ctron -n __ctron_top -l dry-run -d 'validate without writing'
# --dry-run inside the entry groups is offered only where it does something
# (delete would act for real under it)
complete -c ctron -l config-dir -r -f -a '(__fish_complete_directories)' -d 'configuration directory'

complete -c ctron -n __ctron_top -l mode -r -f -a '(__ctron_names modes)' -d 'apply a shortcut bundle'
complete -c ctron -n __ctron_top -l profile -xa 'quiet balanced performance' -d 'platform profile'
complete -c ctron -n __ctron_top -l ac-profile -xa 'off quiet balanced performance' -d 'profile asusd enforces on AC'
complete -c ctron -n __ctron_top -l battery-profile -xa 'off quiet balanced performance' -d 'profile asusd enforces on battery'
complete -c ctron -n __ctron_top -l epp -xa 'power balance_power balance_performance performance' -d 'energy performance preference'
complete -c ctron -n __ctron_top -l freq -xa 'core ccd' -d 'frequency ceiling'
complete -c ctron -n __ctron_top -l hz -xa 'max' -d 'display refresh rate'
complete -c ctron -n __ctron_top -l battery -xa '20 30 40 50 60 70 80 90 100' -d 'battery charge limit'
complete -c ctron -n __ctron_top -l battery-oneshot -d 'charge to 100 % once (asusctl)'
complete -c ctron -n __ctron_top -l ppt -xa 'Q45 B60 P80 45,55,55 60,75,75 80,80,80 off on' -d 'CPU watt limits'
complete -c ctron -n __ctron_top -l nv-boost -xa '5 10 15 20 25' -d 'NVIDIA dynamic boost'
complete -c ctron -n __ctron_top -l nv-temp -xa '75 80 85 87' -d 'NVIDIA temperature target'
complete -c ctron -n __ctron_top -l gpu-clock -xa 'reset' -d 'NVIDIA core-clock lock'
complete -c ctron -n __ctron_top -l panel-od -xa 'on off' -d 'panel overdrive'
complete -c ctron -n __ctron_top -l cpu-boost -xa 'on off' -d 'cpufreq boost'
complete -c ctron -n __ctron_top -l kbd -xa 'off low med high' -d 'keyboard backlight'
complete -c ctron -n __ctron_top -l aura -r -f -a 'static breathe rainbow-cycle rainbow-wave pulse comet flash stars rain highlight laser ripple' -d 'keyboard lighting'
complete -c ctron -n '__ctron_top; and __ctron_after_option_value --aura' -xa 'Ice Red Green Blue Yellow Purple Orange White' -d 'keyboard colour'
complete -c ctron -n __ctron_top -l fan -r -f -a 'stock silent cool full on off cpu gpu (__ctron_names curves)' -d 'fan preset, switch or saved curve'
complete -c ctron -n '__ctron_top; and __ctron_after --fan cpu' -xa 'on off' -d 'custom curve of the CPU fan'
complete -c ctron -n '__ctron_top; and __ctron_after --fan gpu' -xa 'on off' -d 'custom curve of the GPU fan'
complete -c ctron -n __ctron_top -l fan-curve -r -f -a 'cpu gpu' -d 'stage a fan curve'
complete -c ctron -n __ctron_top -l fan-write -d 'write the staged curve to the EC'

complete -c ctron -n __ctron_first -a mode -d 'shortcut bundles: list, show, add, delete'
complete -c ctron -n __ctron_first -a snapshot -d 'hardware snapshots: list, apply, diff, export, delete'
complete -c ctron -n __ctron_first -a profile -d 'alias of snapshot'
complete -c ctron -n __ctron_first -a fancurve -d 'saved fan curves: list, apply, diff, export, delete'
complete -c ctron -n __ctron_first -a fan-curve -d 'alias of fancurve'

# one `complete` line per candidate: a description stops at the next space
set -l mode_sub '__ctron_second mode "list show add delete"'
complete -c ctron -n $mode_sub -f -a list -d 'list the modes'
complete -c ctron -n $mode_sub -f -a show -d 'print one mode'
complete -c ctron -n $mode_sub -f -a add -d 'add a mode'
complete -c ctron -n $mode_sub -f -a delete -d 'delete a mode'
complete -c ctron -n '__fish_seen_subcommand_from mode; and __fish_seen_subcommand_from show delete' -r -f -a '(__ctron_names modes)' -d mode

set -l entry_sub '__ctron_second "snapshot profile fancurve fan-curve" "list apply diff export delete"'
complete -c ctron -n $entry_sub -f -a list -d 'list the entries'
complete -c ctron -n $entry_sub -f -a apply -d 'apply an entry'
complete -c ctron -n $entry_sub -f -a diff -d 'compare with live hardware'
complete -c ctron -n $entry_sub -f -a export -d 'save the live state'
complete -c ctron -n $entry_sub -f -a delete -d 'delete an entry'
complete -c ctron -n '__fish_seen_subcommand_from snapshot profile; and __fish_seen_subcommand_from apply diff export delete' -r -f -a '(__ctron_names snapshots)' -d snapshot
# only apply accepts --dry-run (delete would act for real)
complete -c ctron -n '__fish_seen_subcommand_from snapshot profile; and __fish_seen_subcommand_from apply' -l dry-run -d 'validate without writing'
complete -c ctron -n '__fish_seen_subcommand_from fancurve fan-curve; and __fish_seen_subcommand_from apply diff export delete' -r -f -a '(__ctron_names curves)' -d 'fan curve'
complete -c ctron -n '__fish_seen_subcommand_from fancurve fan-curve; and __fish_seen_subcommand_from apply' -l dry-run -d 'validate without writing'

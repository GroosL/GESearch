This should be really fast (on my tests in was around 10x faster than eza + ripgrep/grep and 2x faster than GNU's ls + ripgrep/grep

# Requirements:
- Any C compiler
- Gnumake
- Hands


# Compilation:
`make`

Or

`cc main.c -o search`


# Usage:
`search <directory> <substring> [-i]`

## Example
`search /nix/store foo`

`search /nix/store "foo bar"`

`search /nix/store FoOBAR -i`

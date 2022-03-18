bashcompletiondir = get_option('bashcompletiondir')
if bashcompletiondir == ''
  bash_completion = dependency('bash-completion', required: false)
  if bash_completion.found()
    bashcompletiondir = bash_completion.get_pkgconfig_variable('completionsdir')
  else
    bashcompletiondir = datadir / 'bash-completion/completions'
  endif
endif

if bashcompletiondir != 'no'
  install_data('mangohud',    install_dir: bashcompletiondir)
  install_data('mangohudctl', install_dir: bashcompletiondir)
endif

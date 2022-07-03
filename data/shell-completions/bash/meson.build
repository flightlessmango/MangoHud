bashcompletiondir = get_option('bashcompletiondir')
if bashcompletiondir == ''
  bash_completion = dependency('bash-completion', required: false)
  bashcompletion_defaultdir = get_option('prefix') / get_option('datadir') / 'bash-completion/completions'
  if bash_completion.found()
    bashcompletiondir = bash_completion.get_variable(pkgconfig: 'completionsdir', default_value: bashcompletion_defaultdir)
  else
    bashcompletiondir = bash_completion_defaultdir
  endif
endif

if bashcompletiondir != 'no'
  install_data('mangohud',    install_dir: bashcompletiondir)
  install_data('mangohudctl', install_dir: bashcompletiondir)
endif

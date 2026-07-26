scdoc = find_program('scdoc', required: get_option('manpages'))

if scdoc.found()
  foreach page : ['pkgbuild_artifact.3', 'pkgbuild_plan_adapter.3']
    source = page + '.scd'
    rendered = custom_target(
      page,
      input: source,
      output: page,
      command: [scdoc],
      feed: true,
      capture: true,
      install: true,
      install_dir: join_paths(get_option('mandir'), 'man3'),
    )
  endforeach
endif

{
  'targets': [
    {
      'target_name': 'eventlog',
      'sources': [
        'src/eventlog.cc'
      ],
      'conditions' : [
        ['OS=="win"', {
          'libraries' : ['advapi32.lib']
        }]
      ]
    }
  ]
}

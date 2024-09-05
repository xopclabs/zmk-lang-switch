# zmk-lang-switch


| :warning:      DOCUMENDATION IS WIP     |
|-----------------------------------------|

This module adds an software layout (i.e. language) switching behaviors to ZMK. 

## Usage

To load the module, add the following entries to `remotes` and `projects` in `config/west.yml`.

```yaml
manifest:
  remotes:
    - name: zmkfirmware
      url-base: https://github.com/zmkfirmware
    - name: xopclabs
      url-base: https://github.com/xopclabs
  projects:
    - name: zmk
      remote: zmkfirmware
      revision: main
      import: app/west.yml
    - name: zmk-lang-switch
      remote: xopclabs
      revision: main
  self:
    path: config
```

## Configuration

- My personal [zmk-config](https://github.com/xopclabs/zmk-config) contains a more advanced example

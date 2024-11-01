# zmk-lang-switch

This module:
- makes custom keyboard layouts possible in multi-language setups (e.g. colemak-dh for English, phonetic-colemak-dh for Russian while having only QWERTY and ЙЦУКЕН installed in OS)
- let's you have a language-independent symbolic layer (i.e. `?` key is always `?` be it on English or on Russian layout) 

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
You'll need to define a keycode for language switching hotkey in your system (`alt+shift` for me on Linux and Windows). I also define layer id's with meaningful names. 3 behaviors (in `.keymap` file or in a separate `.dtsi`):
```dtsi
#define ENG 0
#define RU 1
#define LANGSW LA(LSHIFT)
```
Create a language switch behavior. You'll use it to switch to a target language layer like this `&ls RU`. When pressed, this behavior combines switching to language's layers with pressing `LANGSW` enough times to switch to target language. 

**Keep in mind that keyboard doesn't know anything about actual OS language!** This means that "out of sync" situations might happen (keyboard thinks it's on ENG, but in reality OS is on Russian), which might be resolved by pressing `LANGSW` manually. This also means that you should define your keyboard layers in according to your OS' keyboard layout order. 
```dtsi
ls: lang_switch {
    compatible = "zmk,behavior-lang-switch";
    #binding-cells = <1>;
    bindings = <&kp LANG_SW>;
    layers = <ENG RU>;
};
```
Create a language switch behavior that won't actually switch layer. You'll use it for the next behavior.
```dtsi
Create a language switch
ls_: lang_switch_no_layer {
    compatible = "zmk,behavior-lang-switch";
    #binding-cells = <1>;
    bindings = <&kp LANG_SW>;
    layers = <ENG RU>;
    no-layer-switch;
};
```
Create a press-on-lang behavior for each language. It assures that keycode will be pressed on a specified language. We could use this behavior as `&kp_en QMARK`, language will be switched to English briefly, only to press the question mark keycode (pressing `&kp QMARK` while having, for example, Russian layout turned on results in comma being typed instead)  
```dtsi
kp_en: kp_on_eng {
    compatible = "zmk,behavior-kp-on-lang";
    #binding-cells = <1>;
    bindings = <&ls_ ENG>;
};
```
Finally, you can create a layer for each language (remeber the order!), assign a lang-switch keys (or a combo!) and create a separate language-independent symbolic layer (having them always pressed on English)

## Links
- My personal [zmk-config](https://github.com/xopclabs/zmk-config) contains a more elaborate example

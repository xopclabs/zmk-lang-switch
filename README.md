# zmk-lang-switch

This module:

- Enables custom keyboard layouts for multi-language setups (e.g. Colemak-DH for English and the default ЙЦУКЕН for Russian, while only QWERTY and ЙЦУКЕН are installed in the OS).
- Allows you to have a language-independent symbolic layer (i.e. the `?` key always produces `?`, regardless of whether the layout is English or Russian).

## Usage

To load the module, add the following entries to the `remotes` and `projects` sections in `config/west.yml`:

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

You need to define a keycode for the language-switching hotkey on your system (e.g., `Alt+Shift` on Linux and Windows). It is also advisable to define layer IDs with meaningful names. For example, add the following three definitions in your `.keymap` file or in a separate `.dtsi` file:

```dtsi
#define ENG 0
#define RU 1
#define LANGSW LA(LSHIFT)
```

Create a language switch behavior that you will use to switch to a target language layer (e.g. `&ls RU`). When pressed, this behavior both switches to the specified language layer and sends the `LANGSW` keycode enough times to change the target language.

**Keep in mind that the keyboard does not know the actual OS language!** This means that "out of sync" situations might occur (for example, the keyboard might assume it’s on ENG while the OS is actually set to Russian). This issue can be resolved by pressing `LANGSW` manually. It also means you should define your keyboard layers in the same order as the OS language order.
```dtsi
ls: lang_switch {
    compatible = "zmk,behavior-lang-switch";
    #binding-cells = <1>;
    bindings = <&kp LANG_SW>;
    layers = <ENG RU>;
};
```

Next, create a language switch behavior that does not actually switch the layer. You will use this for the following behavior.
```dtsi
ls_: lang_switch_no_layer {
    compatible = "zmk,behavior-lang-switch";
    #binding-cells = <1>;
    bindings = <&kp LANG_SW>;
    layers = <ENG RU>;
    no-layer-switch;
};
```

Now, create a press-on-lang behavior for each language. This behavior ensures that the keycode is sent using a specified language. For example, you could use it as `&kp_en QMARK` so that the language is temporarily switched to English to press the question mark key (since pressing `&kp QMARK` while using a Russian layout might result in a comma being typed instead).
```dtsi
kp_en: kp_on_eng {
    compatible = "zmk,behavior-kp-on-lang";
    #binding-cells = <1>;
    bindings = <&ls_ ENG>;
};
```

Finally, create a layer for each language (remember the order!), assign language-switch keys (or a combo), and set up a separate language-independent symbolic layer (ensuring that those keys are always interpreted as if in English).

## Usage example
```dtsi
keymap {
    compatible = "zmk,keymap";

    eng_layer { // ENG
        display-name = "English";
        bindings = <
        &kp Q      &kp W      &kp F      &kp P      &kp B          &kp J      &kp L      &kp U      &kp Y      &sl ACCENT
        &kp A      &kp R      &kp S      &kp T      &kp G          &kp M      &kp N      &kp E      &kp I      &kp O
        &kp Z      &kp X      &kp C      &kp D      &kp V          &kp K      &kp H      &kp COMMA  &kp DOT    &kp FSLH
                                   &lt SYMB SPACE   &sl FUNC      &lt NUM ESC  &mt LSHIFT BSPC
        >;
    };

    // RU_* keys are just #defines for each Russian letter on ЙЦУКЕН to QWERTY's English letter on the same place
    // &ru_b_jo and &ru_sh_sch are double taps, don't worry about, I'm weird.
    ru_layer { // RU
        display-name = "Russian";
        bindings = <
        &kp RU_F   &kp RU_YA  &ru_ss_hs  &kp RU_P   &ru_b_jo     &kp RU_H  &kp RU_L  &kp RU_Y    &kp RU_JI  &kp RU_ZH
        &kp RU_A   &kp RU_R   &kp RU_S   &kp RU_T   &kp RU_G     &kp RU_M  &kp RU_N  &kp RU_E    &kp RU_I   &kp RU_O
        &kp RU_CH  &kp RU_C   &kp RU_K   &kp RU_D   &kp RU_YI    &kp RU_Z  &kp RU_V  &ru_sh_sch  &kp RU_YU  &kp RU_IE
                                   &lt SYMB SPACE   &sl FUNC     &lt NUM ESC  &mt LSHIFT BSPC
        >;
    };

    symb_layer { // SYMB
        display-name = "Symbolic";
        bindings = <
        &kp_en TILDE  &kp_en AT    &kp_en PRCNT  &kp_en EXCL   &kp_en HASH      &kp_en SLASH  &kp_en QMARK  &kp_en LT     &kp_en GT    &kp_en BSLH
        &kp_en SQT    &kp_en LBKT  &kp_en RBKT   &kp_en EQUAL  &kp_en PLUS      &kp_en STAR   &kp_en UNDER  &kp_en LPAR   &kp_en RPAR  &kp_en MINUS
        &kp_en COLON  &kp_en SEMI  &kp_en DQT    &kp_en CARET  &kp_en DLLR      &kp_en LBRC   &kp_en RBRC   &kp_en GRAVE  &kp_en AMPS  &kp_en PIPE
                                        &kp SPACE  &kp BSPC    &kp_en PERIOD    &kp_en COMMA
        >;
    };
...
```

## Links

- My personal [zmk-config](https://github.com/xopclabs/zmk-config) contains a more elaborate example.

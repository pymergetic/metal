//! Named `reg_mod!` declare sugar — optional.
//!
//! Hand form (preferred parallel to C): module-local `Export` / `Import`
//! enums as named indexes; see `.cursor/rules/metal-regmod-slot-enums.mdc`
//! and `net/ssh` as reference. Outside the muscle: string names only.

/// Declare a module namespace with named export/import handles over
/// [`crate::RegModStatic`].
///
/// ```ignore
/// pymergetic_metal_reg::reg_mod! {
///     mod ssh = "pymergetic.metal.net.ssh";
///     exports: [listen, close, yield_ = "yield"];
///     imports: [aio_yield = "pymergetic.metal.async"::"yield"];
/// }
/// // ssh::listen.publish(ptr);
/// // ssh::aio_yield.call0();
/// ```
#[macro_export]
macro_rules! reg_mod {
    (
        mod $ns:ident = $name:literal;
        exports: [$($ex:ident $(= $ex_lit:literal)?),* $(,)?];
    ) => {
        $crate::reg_mod! {
            mod $ns = $name;
            exports: [$($ex $(= $ex_lit)?),*];
            imports: [];
        }
    };

    (
        mod $ns:ident = $name:literal;
        exports: [$($ex:ident $(= $ex_lit:literal)?),* $(,)?];
        imports: [];
    ) => {
        #[allow(non_upper_case_globals)]
        pub mod $ns {
            #[allow(unused_imports)]
            use $crate::{RegExport, RegImport, RegModStatic};

            /// Full import path for this module (`RegMod.name`).
            pub const NAME: &'static str = $name;

            pub static STORAGE: RegModStatic<
                { <[()]>::len(&[$($crate::reg_mod!(@unit $ex)),*]) },
                0,
            > = RegModStatic::new(
                [$($crate::reg_mod!(@ex_new $ex $(= $ex_lit)?)),*],
                [],
            );

            $crate::reg_mod!(@export_statics 0usize; $($ex $(= $ex_lit)?),*);
        }
    };

    (
        mod $ns:ident = $name:literal;
        exports: [$($ex:ident $(= $ex_lit:literal)?),* $(,)?];
        imports: [$($im:ident = $im_mod:literal :: $im_fn:literal),+ $(,)?];
    ) => {
        #[allow(non_upper_case_globals)]
        pub mod $ns {
            #[allow(unused_imports)]
            use $crate::{RegExport, RegImport, RegModStatic};

            /// Full import path for this module (`RegMod.name`).
            pub const NAME: &'static str = $name;

            pub static STORAGE: RegModStatic<
                { <[()]>::len(&[$($crate::reg_mod!(@unit $ex)),*]) },
                { <[()]>::len(&[$($crate::reg_mod!(@unit $im)),*]) },
            > = RegModStatic::new(
                [$($crate::reg_mod!(@ex_new $ex $(= $ex_lit)?)),*],
                [$(RegImport::new($im_mod, $im_fn)),*],
            );

            $crate::reg_mod!(@export_statics 0usize; $($ex $(= $ex_lit)?),*);
            $crate::reg_mod!(@import_statics 0usize; $($im),*);
        }
    };

    (@unit $x:ident) => {
        ()
    };

    (@ex_new $name:ident) => {
        $crate::RegExport::new(stringify!($name))
    };
    (@ex_new $name:ident = $lit:literal) => {
        $crate::RegExport::new($lit)
    };

    (@export_statics $idx:expr;) => {};
    (@export_statics $idx:expr; $name:ident $(, $rest:tt)*) => {
        pub static $name: &$crate::RegExport = &STORAGE.exports[$idx];
        $crate::reg_mod!(@export_statics $idx + 1usize; $($rest),*);
    };
    (@export_statics $idx:expr; $name:ident = $lit:literal $(, $rest:tt)*) => {
        pub static $name: &$crate::RegExport = &STORAGE.exports[$idx];
        $crate::reg_mod!(@export_statics $idx + 1usize; $($rest),*);
    };

    (@import_statics $idx:expr;) => {};
    (@import_statics $idx:expr; $name:ident $(, $rest:ident)*) => {
        pub static $name: &$crate::RegImport = &STORAGE.imports[$idx];
        $crate::reg_mod!(@import_statics $idx + 1usize; $($rest),*);
    };
}

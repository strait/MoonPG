package = "MoonPG"
version = "0.1-1"

source = {
    url = "git://github.com/strait/MoonPG.git"
}

description = {
    summary = "PostgreSQL client bindings for Lua.",
    detailed = [[
        This is an example for the LuaRocks tutorial.
        Here we would put a detailed, typically
        paragraph-long description.
    ]],
    homepage = "https://github.com/strait/MoonPG",
    license = "MIT/X11" 
}

dependencies = {
    "lua >= 5.1"
}

build = {
    type = "make",
    install_pass = false,
    install = {
        lib = { "moonpg.so" }
    }
}

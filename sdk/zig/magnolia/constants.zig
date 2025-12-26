// Values are taken from Espressif's newlib headers used by Magnolia applets.
// See: <sys/_default_fcntl.h>, <sys/dirent.h>, <unistd.h>.

pub const fd = struct {
    pub const stdin: c_int = 0;
    pub const stdout: c_int = 1;
    pub const stderr: c_int = 2;
};

pub const seek = struct {
    pub const set: c_int = 0; // SEEK_SET
    pub const cur: c_int = 1; // SEEK_CUR
    pub const end: c_int = 2; // SEEK_END
};

pub const access = struct {
    pub const F_OK: c_int = 0;
    pub const X_OK: c_int = 1;
    pub const W_OK: c_int = 2;
    pub const R_OK: c_int = 4;
};

pub const open = struct {
    pub const O_RDONLY: c_int = 0;
    pub const O_WRONLY: c_int = 1;
    pub const O_RDWR: c_int = 2;

    pub const O_APPEND: c_int = 0x0008;
    pub const O_CREAT: c_int = 0x0200;
    pub const O_TRUNC: c_int = 0x0400;
    pub const O_EXCL: c_int = 0x0800;
    pub const O_SYNC: c_int = 0x2000;
    pub const O_NONBLOCK: c_int = 0x4000;
    pub const O_NOCTTY: c_int = 0x8000;
};

pub const dirent = struct {
    pub const DT_UNKNOWN: u8 = 0;
    pub const DT_REG: u8 = 1;
    pub const DT_DIR: u8 = 2;
};


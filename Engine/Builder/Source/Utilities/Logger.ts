const UseColors = process.stdout.isTTY === true && process.env.NO_COLOR === undefined;
const RESET = UseColors ? "\x1b[0m" : "";
const DIM = UseColors ? "\x1b[2m" : "";
const BOLD = UseColors ? "\x1b[1m" : "";
const RED = UseColors ? "\x1b[31m" : "";
const GREEN = UseColors ? "\x1b[32m" : "";
const YELLOW = UseColors ? "\x1b[33m" : "";
const BLUE = UseColors ? "\x1b[34m" : "";
const CYAN = UseColors ? "\x1b[36m" : "";

export class Logger {
    static Info(Msg: string): void {
        console.log(`${BLUE}[INFO]${RESET} ${Msg}`);
    }

    static Success(Msg: string): void {
        console.log(`${GREEN}[OK]${RESET} ${Msg}`);
    }

    static Warn(Msg: string): void {
        console.log(`${YELLOW}[WARN]${RESET} ${Msg}`);
    }

    static Error(Msg: string): void {
        console.error(`${RED}[ERR]${RESET} ${Msg}`);
    }

    static Dim(Msg: string): void {
        console.log(`${DIM}${Msg}${RESET}`);
    }

    static Title(Msg: string): void {
        console.log(`${BOLD}${CYAN}${Msg}${RESET}`);
    }
}

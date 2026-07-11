<p align="center">
  <img src="https://github.com/user-attachments/assets/507f577f-9567-42cf-b232-c7811c200aa6" width="100%" alt="Vulpin Banner">
</p>

# Vulpin

**Vulpin** is a tiny, *single character command scripting language* that runs on top of **Python** :)🦊!  
It was designed to let you ***write the smallest possible programs*** while still having full programming power! ;)

<p align="center">
  <img src="https://img.shields.io/github/stars/Bat-Script/Vulpin?style=social" alt="GitHub stars">
  <img src="https://img.shields.io/github/last-commit/Bat-Script/Vulpin" alt="Last Commit">
</p>

# <img src="https://github.com/user-attachments/assets/331cfe3c-9bf5-47f8-bc6d-6ae9208bc7c8" width="35" alt="installation"> Installation

* Download ***Vulpin*** from **github** or using the gitclone and some of the stuff...
* Download **Rust** then *Install* it.
* Then **Sync** Vulpin in *system variables*. **Sync it with folder of `Bin`**

<br>

## <img src="https://github.com/user-attachments/assets/61e4dcb7-c53b-4a2a-9986-11a9f6eb566d" width="28" alt="Quick Start"> Quick Start

1. **Create a `.vul` file**. Like `hello.vul`:
   ```basic
   G "Hello from Vul!"
   ```
2. **Run it**:
   ```basic
   python vul.py hello.vul
   ```

If you run `python vul.py` without a filename, it will try to execute `app.vul` in the current directory!
> [!TIP]
> You can remove spaces in your app! dont worry about it! Because if you do that you can build your smallest program like this:
>
> ```G"Hello World"```
---

## <img src="https://github.com/user-attachments/assets/4ede23ff-99d4-451f-b06c-399b6d87512c" width="28" alt="command"> Command Reference

| Char | Name              | Syntax                      | Description                          |
|------|-------------------|-----------------------------|--------------------------------------|
| `G`  | Print             | `G expr`                    | Print expression with newline        |
| `P`  | Print (no nl)     | `P expr`                    | Print expression without newline     |
| `=`  | Assign            | `var=expr`                  | Direct variable assignment           |
| `A`  | Arithmetic assign | `A"var"op expr`             | `var = var op expr`                  |
| `S`  | String replace    | `S"var""old""new"`          | Replace substring in variable        |
| `D`  | Delay / Delete    | `D seconds` / `D"var"`      | Wait or delete a variable            |
| `K`  | Input             | `K"var""prompt""type"`      | Read input from keyboard             |
| `X`  | Execute file      | `X"file.py"`                | Run Python file in background        |
| `Q`  | Quit              | `Q`                         | Exit the program                     |
| `E`  | Error exit        | `E"msg"`                    | Print error message and exit         |
| `U`  | Import            | `U"module"`                 | Import Python module or `.vul` file  |
| `?`  | If / Cond jump    | `? cond` / `? cond J label` | Conditional execution                |
| `:`  | Else              | `:`                         | Else clause                          |
| `;`  | Endif             | `;`                         | End if block                         |
| `@`  | While             | `@ cond`                    | Start while loop                     |
| `&`  | Wend / End for    | `&`                         | End loop                             |
| `O`  | For‑range         | `O var start end [step]`    | Counted loop                         |
| `L`  | Label             | `L name`                    | Define a jump label                  |
| `J`  | Jump              | `J label`                   | Unconditional jump                   |
| `F`  | Function          | `F name(params)`            | Define a function                    |
| `R`  | Return            | `R expr`                    | Return from function                 |
| `~`  | End function      | `~`                         | End function body                    |
| `T`  | Try               | `T`                         | Start try block                      |
| `C`  | Catch             | `C` / `C"var"`              | Catch exception                      |
| `Y`  | End try           | `Y`                         | End try/catch block                  |
| `W`  | Switch            | `W expr`                    | Start switch block                   |
| `V`  | Case              | `V value`                   | Case in switch                       |
| `N`  | Default           | `N`                         | Default case                         |
| `Z`  | End switch        | `Z`                         | End switch block                     |
| `!`  | Python exec       | `! code`                    | Execute raw Python code              |
| `#`  | Comment           | `# text`                    | Inline comment                       |

*Bruh :/ This doesnt look's like command reference of ASM :/ if you think, damn! learn ASM ;-;*

---

## <img src="https://github.com/user-attachments/assets/9b0ffedb-577b-4601-aaac-8991ad977136" width="28" alt="basic syntax"> Basic Syntax

```basic
name="Armin"
G"Hello"           # Prints with newline
P"Loading..."      # Prints without newline
G 5 + 3            # Prints 8
G $name            # Prints value of variable name
```

### <img src="https://github.com/user-attachments/assets/7900697e-d035-4e6e-9855-5a962be6776b" width="24" alt="output"> Output

```text
Hello
Loading...8
Armin
```

---

### <img src="https://github.com/user-attachments/assets/7484cff7-978c-4369-969e-18ec06510231" width="24" alt="input"> Input

```basic
K"user""Your name: "
G"Hi, " + $user

# Typed input (invalid → default value)
K"age""Age: ""I"        # Integer (default 0)
K"price""Price: ""F"    # Float (default 0.0)
K"letter""Guess: ""L"   # Single letter (default "")
```

**Type characters for `K`:**
| Char | Type | Default if invalid |
|------|------|--------------------|
| `I` | Integer | `0` |
| `F` | Float | `0.0` |
| `N` | Number (int/float) | `0` |
| `L` | Single letter | `""` |
| `W` | Word (letters only) | `""` |
| `E` | Lowercase only | `""` |
| `U` | Uppercase only | `""` |
| `A` | Letters + spaces | `""` |
| `P` | Alphanumeric + spaces | `""` |

---

### <img src="https://github.com/user-attachments/assets/cad50866-c2d4-477f-9a03-e0e2c7648269" width="24" alt="vars"> Variables

```basic
x=10               # Direct assignment
name="Vul"

A"x"+5             # x = x + 5
S"name""Vul""VUL"  # Replace in string
D"y"               # Delete variable
```

**String shortcuts:**
| Shortcut | Method | Example (`$msg.S`) |
|----------|--------|-------------------|
| `.U` | `upper()` | `"hello".U` → `"HELLO"` |
| `.L` | `lower()` | `"HELLO".L` → `"hello"` |
| `.S` | `strip()` | `" hi ".S` → `"hi"` |
| `.T` | `title()` | `"hi there".T` → `"Hi There"` |
| `.C` | `capitalize()` | `"hello".C` → `"Hello"` |

### <img src="https://github.com/user-attachments/assets/4ffc9f00-dfca-48ed-99f5-c17fe37a2fde" width="24" alt="delay"> Delay

```basic
D1          # wait 1 second
D0.5        # wait 0.5 seconds
D $delay    # wait the value of variable
```

---

### <img src="https://github.com/user-attachments/assets/f4b14c7a-ed63-4bdd-a769-daa621300529" width="24" alt="modules"> Imports

```basic
U"os"
G $os.getcwd()
$os.system("echo Hello")

U"math"
G $math.sqrt(16)

U"mylib.vul"    # execute another Vul file
```

---

## <img src="https://github.com/user-attachments/assets/9fd18d57-4d07-4897-9f7b-5015e32ff721" width="28" alt="control flow"> Control Flow

### <img src="https://github.com/user-attachments/assets/2ec067a9-d217-4e28-8cec-f0319e93c1f4" width="24" alt="if else"> If / Else

```basic
score=85
? $score >= 90
    G"A"
:
? $score >= 80
    G"B"
:
    G"C"
;
;
```

### <img src="https://github.com/user-attachments/assets/e3d44903-e031-4294-be3a-5618282ffaf3" width="24" alt="jump"> Conditional Jump

```basic
x=5
? $x > 3 J skip
G"Not printed"
L skip
G"Printed"
```

### <img src="https://github.com/user-attachments/assets/2189c536-932b-436f-a207-4dbae2514a2a" width="24" alt="while"> While Loop

```basic
i=0
@ $i < 5
    G $i
    i=$i+1
&
```

### <img src="https://github.com/user-attachments/assets/014d501f-ec7f-42c4-bb54-9cc2e943374c" width="24" alt="infinite loop"> Infinite Loop

```basic
@1
    G"Running forever..."
&
```

### <img src="https://github.com/user-attachments/assets/db70bf74-352a-4d96-924b-59701a7c4c32" width="24" alt="for range"/> For‑Range Loop

```basic
O i 0 5            # 0,1,2,3,4
    G $i
&

O x 10 0 -2        # 10,8,6,4,2
    G $x
&
```

### <img src="https://github.com/user-attachments/assets/610cc6f7-9b0e-475d-8214-d1956108a150" width="24" alt="switch case"> Switch / Case

```basic
fruit="apple"
W $fruit
V"banana"
G"yellow"
V"apple"
G"red or green"
N
G"unknown"
Z
```

### <img src="https://github.com/user-attachments/assets/69071c8e-a5ef-4191-accf-39ac6bf33871" width="24" alt="lables and jumps"> Labels & Jumps

```basic
J end
G"Skipped"
L end
G"Done"
```

---

## <img src="https://github.com/user-attachments/assets/8df0efae-1ea3-480a-8521-1a831273eb4f" width="28" alt="Functions"> Functions

```basic
F add(a, b)
    R $a + $b
~

G $add(3, 4)       # 7

F greet(name)
    G"Hello " + $name
~

$greet("World")
```

---

## <img src="https://github.com/user-attachments/assets/5fd93dca-7f13-42a9-9079-cf73c4a8dd2e" width="28" alt="error handling"> Error Handling

```basic
T
    x=10
    y=0
    G $x/$y        # division by zero!
C"err"
    G"Error: " + $err
Y
G"Continues..."
```

### <img src="https://github.com/user-attachments/assets/7900697e-d035-4e6e-9855-5a962be6776b" width="24" alt="output"> Output

```text
Error: division by zero
Continues...
```

---

## <img src="https://github.com/user-attachments/assets/c4f07e70-bae7-41cc-9d74-7a9b191f52ee" width="28" alt="py with vul"> Inline Python

**Python requirement**

### <img src="https://github.com/user-attachments/assets/adf0f2c1-4fbc-461a-ac28-1133bafd2688" width="24" alt="Single line"> Single line

```basic
!print("Hello from Python")
!x = 42
G $x
```

### <img src="https://github.com/user-attachments/assets/a65cebb9-ada6-4f82-b4bb-f0d6e4a03de9" width="24" alt="multi line"> Multi‑line

```basic
!class Dog:
!    def __init__(self, name):
!        self.name = name
!    def speak(self):
!        return "Woof!"
!
!d = Dog("Buddy")
!print(d.name)
!print(d.speak())
```

---

### <img src="https://github.com/user-attachments/assets/f8cf14d1-0e20-4e81-9abc-87d40cb50371" width="24" alt="vulpin with rust"> Vulpin with rust

```basic
^use std::process::Command;
^fn main() {
^    Command::new("winver")
^        .spawn()
^        .expect("failed to launch winver");
^    println!("winver launched, continuing...");
^}
```

You can use ```^``` for running **Rust** command's!

---
## <img src="https://github.com/user-attachments/assets/9d3da031-73c9-4067-a40d-deebb60c8835" width="28" alt="complete examples"> Complete Examples

### <img src="https://github.com/user-attachments/assets/f8d02dc9-ba0a-4475-a631-ce85ad7165c1" width="24" alt="hello world"> Hello World

```basic
G"Hello World"
```

### <img src="https://github.com/user-attachments/assets/79d007f1-9cca-47da-96c5-1dfa7e5098b7" width="24" alt="guess"> Guessing Game

```vul
U"random"
secret=$random.randint(1,10)
tries=0
L guess
K"num""Guess (1-10): ""I"
tries=$tries+1
? $num=$secret
    G"Correct! Tries: "+$tries
    Q
:? $num<$secret G"Higher"
: G"Lower"
;
;
J guess
```

### <img src="https://github.com/user-attachments/assets/fd3b9fed-4ce3-4f57-b0df-9c8d6b92a55a" width="24" alt="factory"> Factorial

```basic
F factorial(n)
    ? $n<=1
        R 1
    ;
    R $n*$factorial($n-1)
~

G $factorial(5)   # 120
```

---

## <img src="https://github.com/user-attachments/assets/abbb437f-90ee-4ab5-a81d-9af0445dc0eb" width="28" alt="information"> Checking vul version

To check the version of Vul you are running:

```bash
vulpin version
```

Output:
```text
Vul 0.1
```

# <img src="https://github.com/user-attachments/assets/b5b7b282-5fa6-4c58-84dc-3ec68868d236" width="35" alt="build"> Build your apps!

```
vulpin build
```

---

# <img src="https://github.com/user-attachments/assets/c40f82cd-42a4-459f-8527-a6a5d536fa21" width="35" alt="fix"> Troubleshooting

Let's fix your **problems**!

- Python Type Hint Syntax Error:
  this is known error in new Vulpin version like you can see it at most in version `0.5` but thats easy to fix!
  - on ***vulpin 0.5*** this error might be in line of `682` or line of `77` or etc...!
    take a look at here to see how to fix: https://github.com/orgs/community/discussions/199748
---

## <img src="https://github.com/user-attachments/assets/64a4c9c6-0d14-4227-bb4c-4b53892c658b" width="28" alt="sotn"> Some of the *notes*

- **Spaces** are optional after commands. `G"Hi"` and `G "Hi"` both work.
- **All commands are case‑sensitive** – only uppercase for the command letters <mark>(except `!`, `=`, `#`)</mark>.
- **The dot operator** (like `$os.name`) works correctly in the latest release. If you encounter issues, use the `--debug` flag to see detailed parser output.

---

## <img src="https://github.com/user-attachments/assets/205bbb20-10fb-47f9-a08f-025fa6ed92da" width="28" alt="licence"> License

MIT LICENCE.
CHECK OUT LICENCE.
ICONS ARE UNDER LICENCE TOO.

---

<p align="center">
  <img src="https://github.com/user-attachments/assets/2ba6b38a-1295-44e9-9c74-a4bd59697274" width="50" alt="party popper">
</p>

**Happy coding with Vulpin!**

Actually, the word "vulpin" comes from Vulpes. Vulpes are so cute! I was taking a look at them and saw that they have rainbow colored eyes and light eyes! but they were escaping from me. :(
We all learn from animals and nature :D We should support all animals. The fox is not extinct yet, but it could be. If we don't pay attention, it will become extinct too. :(

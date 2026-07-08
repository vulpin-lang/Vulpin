<img width="640" height="320" alt="bn" src="https://github.com/user-attachments/assets/507f577f-9567-42cf-b232-c7811c200aa6" />

# Vulpin

**Vulpin** is a tiny, *single character command scripting language* that runs on top of **Python** :)🦊!  
It was designed to let you ***write the smallest possible programs*** while still having full programming power! ;)

![GitHub stars](https://shields.io/github/stars/Bat-Script/Vulpin)
![Last Commit](https://img.shields.io/github/last-commit/Bat-Script/Vulpin)

# Installition

<img width="50" height="50" alt="instalition" src="https://github.com/user-attachments/assets/331cfe3c-9bf5-47f8-bc6d-6ae9208bc7c8" />

* Download ***Vulpin*** from **github** or using the gitclone and some of the stuff...

* Download **Python 3**.

* Then **Sync** Vulpin in *system variables*. **Sync it with folder of ```Bin```**

<br>

## Quick Start
<img width="50" height="50" alt="Quick Start" src="https://github.com/user-attachments/assets/61e4dcb7-c53b-4a2a-9986-11a9f6eb566d" />

1. **Create a `.vul` file**. Like `hello.vul`:
   ```vul
   G "Hello from Vul!"
   ```
2. **Run it**:
   ```bash
   python vul.py hello.vul
   ```

If you run `python vul.py` without a filename, it will try to execute `app.vul` in the current directory!
> [!TIP]
> You can remove spaces in your app! dont worry about it! Because if you do that you can build your smallest program like this:
>
> ```G"Hello World"```
---

## Command Reference
<img width="50" height="50" alt="command" src="https://github.com/user-attachments/assets/4ede23ff-99d4-451f-b06c-399b6d87512c" />

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

## Basic Syntax
<img width="50" height="50" alt="basic syntax" src="https://github.com/user-attachments/assets/9b0ffedb-577b-4601-aaac-8991ad977136" />


```vul
name="Armin"
G"Hello"           # Prints with newline
P"Loading..."      # Prints without newline
G 5 + 3            # Prints 8
G $name            # Prints value of variable name
```

### Output
<img width="50" height="50" alt="output" src="https://github.com/user-attachments/assets/7900697e-d035-4e6e-9855-5a962be6776b" />

```text
Hello
Loading...8
Armin
```

---

### Input
<img width="50" height="50" alt="input" src="https://github.com/user-attachments/assets/7484cff7-978c-4369-969e-18ec06510231" />

```vul
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

### Variables
<img width="50" height="50" alt="vars" src="https://github.com/user-attachments/assets/cad50866-c2d4-477f-9a03-e0e2c7648269" />


```vul
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

### Delay
<img width="50" height="50" alt="delay" src="https://github.com/user-attachments/assets/4ffc9f00-dfca-48ed-99f5-c17fe37a2fde" />

```vul
D1          # wait 1 second
D0.5        # wait 0.5 seconds
D $delay    # wait the value of variable
```

---

### Imports
<img width="50" height="50" alt="modules" src="https://github.com/user-attachments/assets/f4b14c7a-ed63-4bdd-a769-daa621300529" />


```vul
U"os"
G $os.getcwd()
$os.system("echo Hello")

U"math"
G $math.sqrt(16)

U"mylib.vul"    # execute another Vul file
```

---

## Control Flow
<img width="50" height="63" alt="control flow" src="https://github.com/user-attachments/assets/9fd18d57-4d07-4897-9f7b-5015e32ff721" />


### `?` / `:` / `;` – If / Else

```vul
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

### Conditional Jump
<img width="43" height="50" alt="jump" src="https://github.com/user-attachments/assets/e3d44903-e031-4294-be3a-5618282ffaf3" />

```vul
x=5
? $x > 3 J skip
G"Not printed"
L skip
G"Printed"
```

### While Loop
<img width="43" height="50" alt="while" src="https://github.com/user-attachments/assets/2189c536-932b-436f-a207-4dbae2514a2a" />

```vul
i=0
@ $i < 5
    G $i
    i=$i+1
&
```

### Infinite Loop
<img width="50" height="47" alt="infinite loop" src="https://github.com/user-attachments/assets/014d501f-ec7f-42c4-bb54-9cc2e943374c" />

```vul
@1
    G"Running forever..."
&
```

### For‑Range Loop
<img width="50" height="50" alt="for range" src="https://github.com/user-attachments/assets/db70bf74-352a-4d96-924b-59701a7c4c32" />

```vul
O i 0 5            # 0,1,2,3,4
    G $i
&

O x 10 0 -2        # 10,8,6,4,2
    G $x
&
```

### Switch / Case
<img width="50" height="50" alt="switch case" src="https://github.com/user-attachments/assets/610cc6f7-9b0e-475d-8214-d1956108a150" />


```vul
fruit="apple"
W $fruit
V"banana"   G"yellow"
V"apple"    G"red or green"
N           G"unknown"
Z
```

### Labels & Jumps
<img width="50" height="50" alt="lables and jumps" src="https://github.com/user-attachments/assets/69071c8e-a5ef-4191-accf-39ac6bf33871" />

```vul
J end
G"Skipped"
L end
G"Done"
```

---

## Functions
<img width="50" height="50" alt="Functions" src="https://github.com/user-attachments/assets/8df0efae-1ea3-480a-8521-1a831273eb4f" />

```vul
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

## Error Handling
<img width="50" height="50" alt="error handling" src="https://github.com/user-attachments/assets/5fd93dca-7f13-42a9-9079-cf73c4a8dd2e" />

```vul
T
    x=10
    y=0
    G $x/$y        # division by zero!
C"err"
    G"Error: " + $err
Y
G"Continues..."
```

Output:

<img width="50" height="50" alt="output" src="https://github.com/user-attachments/assets/7900697e-d035-4e6e-9855-5a962be6776b" />

```
Error: division by zero
Continues...
```

---

## Inline Python
<img width="50" height="50" alt="py with vul" src="https://github.com/user-attachments/assets/c4f07e70-bae7-41cc-9d74-7a9b191f52ee" />


### Single line
<img width="50" height="50" alt="Single line" src="https://github.com/user-attachments/assets/adf0f2c1-4fbc-461a-ac28-1133bafd2688" />

```vul
!print("Hello from Python")
!x = 42
G $x
```

### Multi‑line
<img width="50" height="50" alt="multi line" src="https://github.com/user-attachments/assets/a65cebb9-ada6-4f82-b4bb-f0d6e4a03de9" />

```vul
!class Dog:
!    def __init__(self, name):
!        self.name = name
!    def speak(self):
!        return "Woof!"

!d = Dog("Buddy")
G $d.name
G $d.speak()
```

---

## Complete Examples
<img width="50" height="50" alt="complete examples" src="https://github.com/user-attachments/assets/9d3da031-73c9-4067-a40d-deebb60c8835" />


### Hello World
<img width="50" height="50" alt="hello world" src="https://github.com/user-attachments/assets/f8d02dc9-ba0a-4475-a631-ce85ad7165c1" />

```vul
G"Hello World"
```

### Calculator
<img width="50" height="50" alt="calculator" src="https://github.com/user-attachments/assets/b9155a99-ae82-4d97-b629-a40b0f75333b" />

```vul
K"a""First: ""N"
K"op""Op (+,-,*,/): ""W"
K"b""Second: ""N"
? $op="+" G $a+$b
:? $op="-" G $a-$b
:? $op="*" G $a*$b
:? $op="/" G $a/$b
;
;
;
;
```

### Guessing Game
<img width="50" height="50" alt="guess" src="https://github.com/user-attachments/assets/79d007f1-9cca-47da-96c5-1dfa7e5098b7" />

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

### Factorial
<img width="50" height="50" alt="factory" src="https://github.com/user-attachments/assets/fd3b9fed-4ce3-4f57-b0df-9c8d6b92a55a" />

```vul
F factorial(n)
    ? $n<=1
        R 1
    ;
    R $n*$factorial($n-1)
~

G $factorial(5)   # 120
```

---

## Checking vul version
<img width="50" height="50" alt="information" src="https://github.com/user-attachments/assets/abbb437f-90ee-4ab5-a81d-9af0445dc0eb" />

To check the version of Vul you are running:

```bash
vulpin version
```

Output:
```
Vul 0.1
```

# Build your apps! (Beta)
<img width="50" height="50" alt="build" src="https://github.com/user-attachments/assets/b5b7b282-5fa6-4c58-84dc-3ec68868d236" />

* If you want to build your app, first you should install pyinstaller:

```bash
pip install pyinstaller
```

Then all things are right!

### you can build your apps easily like this:

* for build your app as ```default```:
```
vulbuild
```
* for build for ```linux```, ```macos```,```windows```:
```
vulbuild --os all --cross
```
* for build a ```specific OS``` only:
```
vulbuild --os linux --cross
```
```
vulbuild --os windows --cross
```
* for ```package``` your Project:
```
vulbuild --os all --cross --package zip tar.gz appimage dmg
```

---

# TroubleShooting
<img width="50" height="50" alt="fix" src="https://github.com/user-attachments/assets/c40f82cd-42a4-459f-8527-a6a5d536fa21" />


Let's fix your **problems**!

- Python Type Hint Syntax Error:
  this is known error in new Vulpin version like you can see it at most in version ```0.5``` but thats easy to fix!
  - on ***vulpin 0.5*** this error might be in line of ```682``` or line of ```77``` or etc...!
    take a look at here to see how to fix: https://github.com/orgs/community/discussions/199748
---

## Some of the *notes*
<img width="50" height="50" alt="sotn" src="https://github.com/user-attachments/assets/64a4c9c6-0d14-4227-bb4c-4b53892c658b" />

- **Spaces** are optional after commands. `G"Hi"` and `G "Hi"` both work.
- **All commands are case‑sensitive** – only uppercase for the command letters <mark>(except `!`, `=`, `#`)</mark>.
- **The dot operator** (like `$os.name`) works correctly in the latest release. If you encounter issues, use the `--debug` flag to see detailed parser output.

---

## License
<img width="50" height="50" alt="licence" src="https://github.com/user-attachments/assets/205bbb20-10fb-47f9-a08f-025fa6ed92da" />

MIT LICENCE.
CHECK OUT LICENCE.
ICONS ARE UNDER LICENCE TOO.

---

<img width="50" height="50" alt="party popper" src="https://github.com/user-attachments/assets/2ba6b38a-1295-44e9-9c74-a4bd59697274" />

**Happy coding with Vulpin!**

Actually, the word "vulpin" comes from Vulpes. Vulpes are so cute! I was taking a look at them and saw that they have rainbow colored eyes and light eyes! but they were escaping from me. :(
We all learn from animals and nature :D We should support all animals. The fox is not extinct yet, but it could be. If we don't pay attention, it will become extinct too. :(
***

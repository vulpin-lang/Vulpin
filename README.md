<p align="center">
  <img src="https://github.com/user-attachments/assets/bb805288-2c59-46e1-811d-bac9ca7e40db" width="80%" alt="Vulpin Banner">
</p>

<p align="center">
  <strong>Vulpin</strong> is a tiny, <em>single character command scripting language</em> that runs on top of <strong>C</strong>!<br>
  It was designed to let you <strong><em>write the smallest possible program's</em></strong>.
</p>


<p align="center">
    <img src="https://img.shields.io/github/stars/vulpin-lang/Vulpin?style=for-the-badge&color=fab387&labelColor=313244">
    <img src="https://img.shields.io/github/forks/vulpin-lang/Vulpin?style=for-the-badge&color=89dceb&labelColor=313244">
    <img src="https://img.shields.io/github/issues/vulpin-lang/Vulpin?style=for-the-badge&color=f38ba8&labelColor=313244">
    <img src="https://img.shields.io/github/license/vulpin-lang/Vulpin?style=for-the-badge&color=a6e3a1&labelColor=313244">
    <br>
    <img src="https://img.shields.io/github/last-commit/vulpin-lang/Vulpin?style=for-the-badge&color=cba6f7&labelColor=313244">
    <img src="https://img.shields.io/github/repo-size/vulpin-lang/Vulpin?style=for-the-badge&color=74c7ec&labelColor=313244">
    <img src="https://img.shields.io/github/languages/code-size/vulpin-lang/Vulpin?style=for-the-badge&color=f9e2af&labelColor=313244">
</p>

<hr>


# <img src="https://github.com/user-attachments/assets/331cfe3c-9bf5-47f8-bc6d-6ae9208bc7c8" width="35" alt="installation"> Installation
- <p>Go to Vulpin Website</p>
<link href="https://vulpin.fluxCast.dev"/>

- Then tab to the Download button on novbar

- and Download and Install the version of Vulpin that you want! 

or Download Installer!

```
pip install vulpin
```
```

```
## <img src="https://github.com/user-attachments/assets/61e4dcb7-c53b-4a2a-9986-11a9f6eb566d" width="28" alt="Quick Start"> Quick Start

1. **Create a `.vul` file**. Like `hello.vul`:
   ```basic
   G "Hello World!"
   ```
2. **Run it**:
   ```basic
   vulpin hello.vul
   ```

> [!TIP]
> You can remove spaces in your app! dont worry about it! Because if you do that you can build your smallest program like this:
>
> ```G"Hello World"```

---



## <img src="https://github.com/user-attachments/assets/9b0ffedb-577b-4601-aaac-8991ad977136" width="28" alt="basic syntax"> Basic Syntax

```basic
name="Armin"
G"Hello"           # Prints with newline
P"Loading..."      # Prints without newline
G 5 + 3            # Prints 8
Gname            # Prints value of variable name
```

---

### <img src="https://github.com/user-attachments/assets/7900697e-d035-4e6e-9855-5a962be6776b" width="24" alt="output"> Output

```text
Hello
Loading...8
Armin
```

---

### <img src="https://github.com/user-attachments/assets/7484cff7-978c-4369-969e-18ec06510231" width="24" alt="input"> Input

```basic
K user "Your name: "
G"Hi, " + user

# Typed input (invalid → default value)
K age"Age: "I        # Integer (default 0)
K price"Price: "F    # Float (default 0.0)
K letter"Guess: "L   # Single letter (default "")

Kinput"K can write like this! :)"
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
D delay    # wait the value of variable
```

---


### <img src="https://github.com/user-attachments/assets/f4b14c7a-ed63-4bdd-a769-daa621300529" width="24" alt="modules"> Imports

You can Import C or Vulpin Modules with using ```U```.

```basic
U"os"
G os.getcwd()
os.system("echo Hello")

U"math"
G math.sqrt(16)
```


## <img src="https://github.com/user-attachments/assets/9fd18d57-4d07-4897-9f7b-5015e32ff721" width="28" alt="control flow"> Control Flow


### <img src="https://github.com/user-attachments/assets/2ec067a9-d217-4e28-8cec-f0319e93c1f4" width="24" alt="if else"> If / Else

```basic
score=85
? score >= 90
    G"A"
:
? score >= 80
    G"B"
:
    G"C"
;
;
```


### <img src="https://github.com/user-attachments/assets/e3d44903-e031-4294-be3a-5618282ffaf3" width="24" alt="jump"> Conditional Jump

```basic
x=5
? x > 3 J skip
G"Not printed"
L skip
G"Printed"
```

### <img src="https://github.com/user-attachments/assets/2189c536-932b-436f-a207-4dbae2514a2a" width="24" alt="while"> While Loop

```basic
i=0
@ i < 5
    G i
    i=i+1
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
    G i # or Gi
&

O x 10 0 -2        # 10,8,6,4,2
    G x # or Gx
&
```

### <img src="https://github.com/user-attachments/assets/610cc6f7-9b0e-475d-8214-d1956108a150" width="24" alt="switch case"> Switch / Case

```basic
fruit="apple"
W fruit
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
    R a + b
~

G add(3, 4)       # 7

F greet(name)
    G"Hello " + name
~

greet("World")
```

---

## <img src="https://github.com/user-attachments/assets/5fd93dca-7f13-42a9-9079-cf73c4a8dd2e" width="28" alt="error handling"> Error Handling

```basic
T
    x=10
    y=0
    G x/y        # division by zero!
C"err"
    G"Error: " + err
Y
G"Continues..."
```

### <img src="https://github.com/user-attachments/assets/7900697e-d035-4e6e-9855-5a962be6776b" width="24" alt="output"> Output

```text
Error: division by zero
Continues...
```

---


## <img src="https://github.com/user-attachments/assets/9d3da031-73c9-4067-a40d-deebb60c8835" width="28" alt="complete examples"> Complete Examples

### <img src="https://github.com/user-attachments/assets/f8d02dc9-ba0a-4475-a631-ce85ad7165c1" width="24" alt="hello world"> Hello World

```basic
G"Hello World"
```

### <img src="https://github.com/user-attachments/assets/79d007f1-9cca-47da-96c5-1dfa7e5098b7" width="24" alt="guess"> Guessing Game

```vul
U"random"
secret=random.randint(1,10)
tries=0
L guess
K"num""Guess (1-10): ""I"
tries=tries+1
? num=secret
    G"Correct! Tries: "+tries
    Q
:? num<secret G"Higher"
: G"Lower"
;
;
J guess
```

### <img src="https://github.com/user-attachments/assets/fd3b9fed-4ce3-4f57-b0df-9c8d6b92a55a" width="24" alt="factory"> Factorial

```basic
F factorial(n)
    ? n<=1
        R 1
    ;
    R n*factorial($n-1)
~

G factorial(5)   # 120
```

---

## <img src="https://github.com/user-attachments/assets/abbb437f-90ee-4ab5-a81d-9af0445dc0eb" width="28" alt="information"> Checking vul version

To check the version of Vul you are running:

```bash
vulpin version
```

Output:
```text
Vul 0.8
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
- **The dot operator** (like `os.name`) works correctly in the latest release. If you encounter issues, use the `--debug` flag to see detailed parser output.

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

# Sponsers:
FluxCast

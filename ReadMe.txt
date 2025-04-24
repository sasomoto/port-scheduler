---

# 🚢 Dock Scheduler for Emergency Ship Handling (OS Assignment)

This project implements a **dock scheduling system** in C using **shared memory**, **message queues**, and **multithreading** to handle ships arriving at a port, prioritize emergency ships, and simulate cargo movement using cranes.

### 🧠 Built for:  
> Operating Systems assignment that tests concepts like IPC, threading, and synchronization in a real-world simulation.

---

## 📦 Features

- ⛴️ Handles **incoming and outgoing ships**
- ⚠️ Prioritizes **emergency ships**
- 🏗️ Simulates **cranes with weight constraints** for cargo handling
- ⏱️ Processes actions across **timesteps**
- 🔐 Uses **message queues** and **shared memory**
- 🧵 Uses **multithreading** to brute-force guess authentication strings (base-6 encoding)
- 🧠 Implements **greedy + matching-based scheduling** for max efficiency

---

## 📁 File Structure

| File | Description |
|------|-------------|
| `scheduler.c` | Main scheduler logic |
| `validation.out` | Provided validation module (acts as judge/tester) |
| `testcase1/input.txt` | Sample testcase input (shared memory keys, dock config) |
| `README.md` | You're reading it 👀 |

---

## ⚙️ How It Works

### 💡 Problem:
Ships (incoming/outgoing, emergency/regular) arrive at a dock. Cranes of various capacities move their cargo. Emergency ships must be docked **as soon as possible**.

### 🔁 Timestep-Based Processing:
Each timestep includes:
- Receiving ship requests
- Prioritizing emergency ships
- Docking ships if docks are free
- Moving cargo with available cranes
- Undocking once all cargo is moved
- Guessing **auth strings** for undocking

### 🔐 Authentication Guessing:
- Uses **base-6** guessing (digits `5 6 7 8 9 .`)
- Multi-threaded brute-force across solvers
- Validates guess via message queues

---

## 🧪 How to Run

```bash
gcc -o scheduler scheduler.c -lrt -pthread
./validation.out 1     # Start validation (first terminal)
./scheduler 1          # Run your scheduler (second terminal)
```

Make sure both are compiled and the input file (`input.txt`) is in the correct `testcase1/` folder.

---

## 📬 Interprocess Communication (IPC)

| Type | Usage |
|------|-------|
| 🧠 **Shared Memory** | Used to share ship requests & auth string guesses |
| 💬 **Message Queues** | Used to communicate between scheduler, validation, and solvers |
| 🧵 **Threads** | Used for parallel guessing of auth strings |

---

## 🔢 Message Types Used

| `mtype` | Purpose |
|--------|---------|
| `1` | Validation → Scheduler (ship requests) |
| `2` | Scheduler → Validation (dock) |
| `3` | Scheduler → Validation (undock) |
| `4` | Scheduler → Validation (cargo movement) |
| `5` | Scheduler → Validation (timestep end) |
| `1/2/3` | Scheduler ↔ Solver (dock set, guess, response) |

---

## 🎓 Concepts Practiced

- Shared Memory (`shmget`, `shmat`)
- Message Queues (`msgget`, `msgsnd`, `msgrcv`)
- Multithreading (`pthread_create`, `pthread_join`)
- Synchronization across processes
- Priority Queues & Scheduling Algorithms
- String guessing with base encoding

---

## 📌 Notes

- Emergency ships are prioritized **every timestep**
- Message queue is implemented with a **sorted linked list**
- Cranes are sorted by capacity, cargo sorted by weight for greedy allocation

---

## 🧠 Potential Viva/Interview Questions

1. How are emergency ships handled?
2. Why use base-6 for guessing?
3. What IPC mechanisms are used?
4. What does each `mtype` mean?
5. How are messages kept in sync per timestep?
6. Why multithreading for auth string guessing?

---

## 🧑‍💻 Author

**Shashwat**  
Made with 💡, caffeine ☕, and lots of debugging 🐞.

---

Would you like me to make a GitHub-flavored version with emojis and collapsible sections too?
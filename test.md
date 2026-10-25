# Markdown Viewer Test

This is a **test document** for the FAR Manager Markdown Viewer plugin.

## Features

The plugin supports the following markdown elements:

### Headers
- Header 1 through Header 6
- Different colors for each level

### Text Formatting
- **Bold text** using double asterisks
- *Italic text* using single asterisks
- `Inline code` using backticks
- [Links](https://example.com) using bracket syntax

### Code Blocks

C++ example:

```cpp
#include <iostream>
#include <vector>
#include <string>

// A simple class example
class Calculator {
private:
    double result;

public:
    Calculator() : result(0.0) {}

    double add(double a, double b) {
        result = a + b;
        return result;
    }

    /* Multi-line
       comment */
    double multiply(double a, double b) {
        return a * b;
    }
};

int main() {
    Calculator calc;
    std::vector<int> numbers = {1, 2, 3, 4, 5};
    int sum = 0;

    for (const auto& num : numbers) {
        sum += num;
    }

    std::cout << "Sum: " << sum << std::endl;
    std::cout << "Result: " << calc.add(10.5, 20.3) << std::endl;

    #define MAX_VALUE 100
    const char* message = "Hello, World!";

    return 0;
}
```

Python example:

```python
import os
import sys
from typing import List, Dict, Optional

# Simple function with type hints
def fibonacci(n: int) -> List[int]:
    """Generate fibonacci sequence up to n terms."""
    if n <= 0:
        return []
    elif n == 1:
        return [0]

    sequence = [0, 1]
    for i in range(2, n):
        sequence.append(sequence[i-1] + sequence[i-2])

    return sequence

class DataProcessor:
    def __init__(self, data: Dict[str, any]):
        self._data = data
        self._cache = {}

    def process(self, key: str) -> Optional[str]:
        if key in self._cache:
            return self._cache[key]

        result = self._data.get(key, None)
        if result is not None:
            self._cache[key] = str(result).upper()

        return self._cache.get(key)

# Main execution
if __name__ == "__main__":
    numbers = fibonacci(10)
    print(f"Fibonacci: {numbers}")

    data = {"name": "test", "value": 42}
    processor = DataProcessor(data)
    print(f"Processed: {processor.process('name')}")
```

JavaScript example:

```javascript
// Async function with promises
async function fetchData(url) {
    try {
        const response = await fetch(url);
        if (!response.ok) {
            throw new Error(`HTTP error! status: ${response.status}`);
        }
        const data = await response.json();
        return data;
    } catch (error) {
        console.error('Fetch failed:', error.message);
        return null;
    }
}

class EventEmitter {
    constructor() {
        this.events = {};
    }

    on(event, callback) {
        if (!this.events[event]) {
            this.events[event] = [];
        }
        this.events[event].push(callback);
    }

    emit(event, ...args) {
        if (this.events[event]) {
            this.events[event].forEach(cb => cb(...args));
        }
    }
}

const PI = 3.14159;
const greeting = `Hello, ${name}!`;
let counter = 0;

export { fetchData, EventEmitter, PI };
```

Rust example:

```rust
use std::collections::HashMap;
use std::io::{self, Read, Write};

#[derive(Debug, Clone)]
struct Config {
    name: String,
    value: i32,
    enabled: bool,
}

impl Config {
    fn new(name: &str, value: i32) -> Self {
        Config {
            name: name.to_string(),
            value,
            enabled: true,
        }
    }
}

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut map: HashMap<String, Config> = HashMap::new();

    let config = Config::new("test", 42);
    map.insert("key1".to_string(), config);

    for (key, value) in &map {
        println!("{}: {:?}", key, value);
    }

    let numbers: Vec<i32> = (0..10).filter(|x| x % 2 == 0).collect();
    println!("Even numbers: {:?}", numbers);

    Ok(())
}
```

### Lists

Unordered list:
- Item one
- Item two
- Item three

Ordered list:
1. First item
2. Second item
3. Third item

### Blockquotes

> This is a blockquote.
> It can span multiple lines.
> > Nested blockquotes work too.

### Horizontal Rules

---

### Tables

| Column 1 | Column 2 | Column 3 |
|----------|----------|----------|
| Cell 1   | Cell 2   | Cell 3   |
| Cell 4   | Cell 5   | Cell 6   |

### Images

![Alt text](image.png)

---

*End of test document*

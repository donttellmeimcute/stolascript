def longest_collatz(limit):
    best = 0
    best_n = 0
    for start in range(1, limit):
        n = start
        steps = 0
        while n != 1:
            if n % 2 == 0:
                n = n // 2
            else:
                n = 3 * n + 1
            steps += 1
        if steps > best:
            best = steps
            best_n = start
    return best_n
print(longest_collatz(1000000))

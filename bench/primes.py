def count_primes(limit):
    count = 0
    for n in range(2, limit):
        d = 2
        p = True
        while d * d <= n:
            if n % d == 0:
                p = False
                break
            d += 1
        if p:
            count += 1
    return count
print(count_primes(2000000))

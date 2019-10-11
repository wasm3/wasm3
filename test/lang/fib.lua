local function fib(n)
    if n < 2 then
        return n
    end
    return fib(n-1) + fib(n-2)
end

print(fib(tonumber(arg[1])))

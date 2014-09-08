
def block_break
    puts '----'
    (1..5).each do |i|
        break if i % 3 == 0
        throw String.new("aa")
        puts i
    end
    puts 'end'
end

block_break

# encoding: UTF-8

require 'member'

class Comp

  attr_reader :name

  def initialize(xe, spec)
    @name = xe.attributes['name']
    @members = []
    xe.nodes.each { |n|
      next unless ('field' == n.name || 'group' == n.name || 'component' == n.name)
      @members << Member.new(n)
    }
  end

  def gen_c(f)
    f.write("        #{@name}")
  end

end # Comp

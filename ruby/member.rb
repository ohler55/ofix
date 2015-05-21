# encoding: UTF-8

class Member
  attr_accessor :kind
  attr_accessor :name
  attr_accessor :required
  attr_accessor :members

  def initialize(xe)
    @name = xe.attributes['name']
    @kind = xe.name
    @required = ('Y' == xe.attributes['required'])
    @members = []
    xe.nodes.each { |n|
      next unless ('field' == n.name || 'group' == n.name || 'component' == n.name)
      @members << Member.new(n)
    }
  end

end # Member

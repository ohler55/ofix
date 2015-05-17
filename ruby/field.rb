# encoding: UTF-8

class Field

  attr_reader :name


  def initialize(xe)
    puts "*** read in #{xe.name} - #{xe.attributes}"
    xe.nodes.each { |n|
      puts "*** read in #{n.name}"
    }
    # TBD build field from XML
    @tag = xe.attributes['number'].to_i
    @name = xe.attributes['name']
    @type = xe.attributes['type']
    @where = 'Body'
    @desc = xe.attributes['description']
    @related = 0
  end

end # Field
